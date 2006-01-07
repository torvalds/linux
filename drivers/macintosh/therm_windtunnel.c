/* 
 *   Creation Date: <2003/03/14 20:54:13 samuel>
 *   Time-stamp: <2004/03/20 14:20:59 samuel>
 *   
 *	<therm_windtunnel.c>
 *	
 *	The G4 "windtunnel" has a single fan controlled by an
 *	ADM1030 fan controller and a DS1775 thermostat.
 *
 *	The fan controller is equipped with a temperature sensor
 *	which measures the case temperature. The DS1775 sensor
 *	measures the CPU temperature. This driver tunes the
 *	behavior of the fan. It is based upon empirical observations
 *	of the 'AppleFan' driver under Mac OS X.
 *
 *	WARNING: This driver has only been testen on Apple's
 *	1.25 MHz Dual G4 (March 03). It is tuned for a CPU
 *	temperatur around 57 C.
 *
 *   Copyright (C) 2003, 2004 Samuel Rydh (samuel@ibrium.se)
 *
 *   Loosely based upon 'thermostat.c' written by Benjamin Herrenschmidt
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/sections.h>
#include <asm/of_device.h>
#include <asm/macio.h>

#define LOG_TEMP		0			/* continously log temperature */

#define I2C_DRIVERID_G4FAN	0x9001			/* fixme */

static int 			do_probe( struct i2c_adapter *adapter, int addr, int kind);

/* scan 0x48-0x4f (DS1775) and 0x2c-2x2f (ADM1030) */
static unsigned short		normal_i2c[] = { 0x48, 0x49, 0x4a, 0x4b,
						 0x4c, 0x4d, 0x4e, 0x4f,
						 0x2c, 0x2d, 0x2e, 0x2f,
						 I2C_CLIENT_END };

I2C_CLIENT_INSMOD;

static struct {
	volatile int		running;
	struct completion	completion;
	pid_t			poll_task;
	
	struct semaphore 	lock;
	struct of_device	*of_dev;
	
	struct i2c_client	*thermostat;
	struct i2c_client	*fan;

	int			overheat_temp;		/* 100% fan at this temp */
	int			overheat_hyst;
	int			temp;
	int			casetemp;
	int			fan_level;		/* active fan_table setting */

	int			downind;
	int			upind;

	int			r0, r1, r20, r23, r25;	/* saved register */
} x;

#define T(x,y)			(((x)<<8) | (y)*0x100/10 )

static struct {
	int			fan_down_setting;
	int			temp;
	int			fan_up_setting;
} fan_table[] = {
	{ 11, T(0,0),  11 },	/* min fan */
	{ 11, T(55,0), 11 },
	{  6, T(55,3), 11 },
	{  7, T(56,0), 11 },
	{  8, T(57,0), 8 },
	{  7, T(58,3), 7 },
	{  6, T(58,8), 6 },
	{  5, T(59,2), 5 },
	{  4, T(59,6), 4 },
	{  3, T(59,9), 3 },
	{  2, T(60,1), 2 },
	{  1, 0xfffff, 1 }	/* on fire */
};

static void
print_temp( const char *s, int temp )
{
	printk("%s%d.%d C", s ? s : "", temp>>8, (temp & 255)*10/256 );
}

static ssize_t
show_cpu_temperature( struct device *dev, struct device_attribute *attr, char *buf )
{
	return sprintf(buf, "%d.%d\n", x.temp>>8, (x.temp & 255)*10/256 );
}

static ssize_t
show_case_temperature( struct device *dev, struct device_attribute *attr, char *buf )
{
	return sprintf(buf, "%d.%d\n", x.casetemp>>8, (x.casetemp & 255)*10/256 );
}

static DEVICE_ATTR(cpu_temperature, S_IRUGO, show_cpu_temperature, NULL );
static DEVICE_ATTR(case_temperature, S_IRUGO, show_case_temperature, NULL );



/************************************************************************/
/*	controller thread						*/
/************************************************************************/

static int
write_reg( struct i2c_client *cl, int reg, int data, int len )
{
	u8 tmp[3];

	if( len < 1 || len > 2 || data < 0 )
		return -EINVAL;

	tmp[0] = reg;
	tmp[1] = (len == 1) ? data : (data >> 8);
	tmp[2] = data;
	len++;
	
	if( i2c_master_send(cl, tmp, len) != len )
		return -ENODEV;
	return 0;
}

static int
read_reg( struct i2c_client *cl, int reg, int len )
{
	u8 buf[2];

	if( len != 1 && len != 2 )
		return -EINVAL;
	buf[0] = reg;
	if( i2c_master_send(cl, buf, 1) != 1 )
		return -ENODEV;
	if( i2c_master_recv(cl, buf, len) != len )
		return -ENODEV;
	return (len == 2)? ((unsigned int)buf[0] << 8) | buf[1] : buf[0];
}

static void
tune_fan( int fan_setting )
{
	int val = (fan_setting << 3) | 7;

	/* write_reg( x.fan, 0x24, val, 1 ); */
	write_reg( x.fan, 0x25, val, 1 );
	write_reg( x.fan, 0x20, 0, 1 );
	print_temp("CPU-temp: ", x.temp );
	if( x.casetemp )
		print_temp(", Case: ", x.casetemp );
	printk(",  Fan: %d (tuned %+d)\n", 11-fan_setting, x.fan_level-fan_setting );

	x.fan_level = fan_setting;
}

static void
poll_temp( void )
{
	int temp, i, level, casetemp;

	temp = read_reg( x.thermostat, 0, 2 );

	/* this actually occurs when the computer is loaded */
	if( temp < 0 )
		return;

	casetemp = read_reg(x.fan, 0x0b, 1) << 8;
	casetemp |= (read_reg(x.fan, 0x06, 1) & 0x7) << 5;

	if( LOG_TEMP && x.temp != temp ) {
		print_temp("CPU-temp: ", temp );
		print_temp(", Case: ", casetemp );
		printk(",  Fan: %d\n", 11-x.fan_level );
	}
	x.temp = temp;
	x.casetemp = casetemp;

	level = -1;
	for( i=0; (temp & 0xffff) > fan_table[i].temp ; i++ )
		;
	if( i < x.downind )
		level = fan_table[i].fan_down_setting;
	x.downind = i;

	for( i=0; (temp & 0xffff) >= fan_table[i+1].temp ; i++ )
		;
	if( x.upind < i )
		level = fan_table[i].fan_up_setting;
	x.upind = i;

	if( level >= 0 )
		tune_fan( level );
}


static void
setup_hardware( void )
{
	int val;

	/* save registers (if we unload the module) */
	x.r0 = read_reg( x.fan, 0x00, 1 );
	x.r1 = read_reg( x.fan, 0x01, 1 );
	x.r20 = read_reg( x.fan, 0x20, 1 );
	x.r23 = read_reg( x.fan, 0x23, 1 );
	x.r25 = read_reg( x.fan, 0x25, 1 );

	/* improve measurement resolution (convergence time 1.5s) */
	if( (val=read_reg(x.thermostat, 1, 1)) >= 0 ) {
		val |= 0x60;
		if( write_reg( x.thermostat, 1, val, 1 ) )
			printk("Failed writing config register\n");
	}
	/* disable interrupts and TAC input */
	write_reg( x.fan, 0x01, 0x01, 1 );
	/* enable filter */
	write_reg( x.fan, 0x23, 0x91, 1 );
	/* remote temp. controls fan */
	write_reg( x.fan, 0x00, 0x95, 1 );

	/* The thermostat (which besides measureing temperature controls
	 * has a THERM output which puts the fan on 100%) is usually
	 * set to kick in at 80 C (chip default). We reduce this a bit
	 * to be on the safe side (OSX doesn't)...
	 */
	if( x.overheat_temp == (80 << 8) ) {
		x.overheat_temp = 65 << 8;
		x.overheat_hyst = 60 << 8;
		write_reg( x.thermostat, 2, x.overheat_hyst, 2 );
		write_reg( x.thermostat, 3, x.overheat_temp, 2 );

		print_temp("Reducing overheating limit to ", x.overheat_temp );
		print_temp(" (Hyst: ", x.overheat_hyst );
		printk(")\n");
	}

	/* set an initial fan setting */
	x.downind = 0xffff;
	x.upind = -1;
	/* tune_fan( fan_up_table[x.upind].fan_setting ); */

	device_create_file( &x.of_dev->dev, &dev_attr_cpu_temperature );
	device_create_file( &x.of_dev->dev, &dev_attr_case_temperature );
}

static void
restore_regs( void )
{
	device_remove_file( &x.of_dev->dev, &dev_attr_cpu_temperature );
	device_remove_file( &x.of_dev->dev, &dev_attr_case_temperature );

	write_reg( x.fan, 0x01, x.r1, 1 );
	write_reg( x.fan, 0x20, x.r20, 1 );
	write_reg( x.fan, 0x23, x.r23, 1 );
	write_reg( x.fan, 0x25, x.r25, 1 );
	write_reg( x.fan, 0x00, x.r0, 1 );
}

static int
control_loop( void *dummy )
{
	daemonize("g4fand");

	down( &x.lock );
	setup_hardware();

	while( x.running ) {
		up( &x.lock );

		msleep_interruptible(8000);
		
		down( &x.lock );
		poll_temp();
	}

	restore_regs();
	up( &x.lock );

	complete_and_exit( &x.completion, 0 );
}


/************************************************************************/
/*	i2c probing and setup						*/
/************************************************************************/

static int
do_attach( struct i2c_adapter *adapter )
{
	int ret = 0;

	if( strncmp(adapter->name, "uni-n", 5) )
		return 0;

	if( !x.running ) {
		ret = i2c_probe( adapter, &addr_data, &do_probe );
		if( x.thermostat && x.fan ) {
			x.running = 1;
			init_completion( &x.completion );
			x.poll_task = kernel_thread( control_loop, NULL, SIGCHLD | CLONE_KERNEL );
		}
	}
	return ret;
}

static int
do_detach( struct i2c_client *client )
{
	int err;

	if( (err=i2c_detach_client(client)) )
		printk(KERN_ERR "failed to detach thermostat client\n");
	else {
		if( x.running ) {
			x.running = 0;
			wait_for_completion( &x.completion );
		}
		if( client == x.thermostat )
			x.thermostat = NULL;
		else if( client == x.fan )
			x.fan = NULL;
		else {
			printk(KERN_ERR "g4fan: bad client\n");
		}
		kfree( client );
	}
	return err;
}

static struct i2c_driver g4fan_driver = {  
	.driver = {
		.name	= "therm_windtunnel",
	},
	.id		= I2C_DRIVERID_G4FAN,
	.attach_adapter = do_attach,
	.detach_client	= do_detach,
};

static int
attach_fan( struct i2c_client *cl )
{
	if( x.fan )
		goto out;

	/* check that this is an ADM1030 */
	if( read_reg(cl, 0x3d, 1) != 0x30 || read_reg(cl, 0x3e, 1) != 0x41 )
		goto out;
	printk("ADM1030 fan controller [@%02x]\n", cl->addr );

	strlcpy( cl->name, "ADM1030 fan controller", sizeof(cl->name) );

	if( !i2c_attach_client(cl) )
		x.fan = cl;
 out:
	if( cl != x.fan )
		kfree( cl );
	return 0;
}

static int
attach_thermostat( struct i2c_client *cl ) 
{
	int hyst_temp, os_temp, temp;

	if( x.thermostat )
		goto out;

	if( (temp=read_reg(cl, 0, 2)) < 0 )
		goto out;
	
	/* temperature sanity check */
	if( temp < 0x1600 || temp > 0x3c00 )
		goto out;
	hyst_temp = read_reg(cl, 2, 2);
	os_temp = read_reg(cl, 3, 2);
	if( hyst_temp < 0 || os_temp < 0 )
		goto out;

	printk("DS1775 digital thermometer [@%02x]\n", cl->addr );
	print_temp("Temp: ", temp );
	print_temp("  Hyst: ", hyst_temp );
	print_temp("  OS: ", os_temp );
	printk("\n");

	x.temp = temp;
	x.overheat_temp = os_temp;
	x.overheat_hyst = hyst_temp;
	
	strlcpy( cl->name, "DS1775 thermostat", sizeof(cl->name) );

	if( !i2c_attach_client(cl) )
		x.thermostat = cl;
out:
	if( cl != x.thermostat )
		kfree( cl );
	return 0;
}

static int
do_probe( struct i2c_adapter *adapter, int addr, int kind )
{
	struct i2c_client *cl;

	if( !i2c_check_functionality(adapter, I2C_FUNC_SMBUS_WORD_DATA
				     | I2C_FUNC_SMBUS_WRITE_BYTE) )
		return 0;

	if( !(cl=kmalloc(sizeof(*cl), GFP_KERNEL)) )
		return -ENOMEM;
	memset( cl, 0, sizeof(struct i2c_client) );

	cl->addr = addr;
	cl->adapter = adapter;
	cl->driver = &g4fan_driver;
	cl->flags = 0;

	if( addr < 0x48 )
		return attach_fan( cl );
	return attach_thermostat( cl );
}


/************************************************************************/
/*	initialization / cleanup					*/
/************************************************************************/

static int
therm_of_probe( struct of_device *dev, const struct of_device_id *match )
{
	return i2c_add_driver( &g4fan_driver );
}

static int
therm_of_remove( struct of_device *dev )
{
	return i2c_del_driver( &g4fan_driver );
}

static struct of_device_id therm_of_match[] = {{
	.name		= "fan",
	.compatible	= "adm1030"
    }, {}
};

static struct of_platform_driver therm_of_driver = {
	.name		= "temperature",
	.match_table	= therm_of_match,
	.probe		= therm_of_probe,
	.remove		= therm_of_remove,
};

struct apple_thermal_info {
	u8		id;			/* implementation ID */
	u8		fan_count;		/* number of fans */
	u8		thermostat_count;	/* number of thermostats */
	u8		unused;
};

static int __init
g4fan_init( void )
{
	struct apple_thermal_info *info;
	struct device_node *np;

	init_MUTEX( &x.lock );

	if( !(np=of_find_node_by_name(NULL, "power-mgt")) )
		return -ENODEV;
	info = (struct apple_thermal_info*)get_property(np, "thermal-info", NULL);
	of_node_put(np);

	if( !info || !machine_is_compatible("PowerMac3,6") )
		return -ENODEV;

	if( info->id != 3 ) {
		printk(KERN_ERR "therm_windtunnel: unsupported thermal design %d\n", info->id );
		return -ENODEV;
	}
	if( !(np=of_find_node_by_name(NULL, "fan")) )
		return -ENODEV;
	x.of_dev = of_platform_device_create(np, "temperature", NULL);
	of_node_put( np );

	if( !x.of_dev ) {
		printk(KERN_ERR "Can't register fan controller!\n");
		return -ENODEV;
	}

	of_register_driver( &therm_of_driver );
	return 0;
}

static void __exit
g4fan_exit( void )
{
	of_unregister_driver( &therm_of_driver );

	if( x.of_dev )
		of_device_unregister( x.of_dev );
}

module_init(g4fan_init);
module_exit(g4fan_exit);

MODULE_AUTHOR("Samuel Rydh <samuel@ibrium.se>");
MODULE_DESCRIPTION("Apple G4 (windtunnel) fan controller");
MODULE_LICENSE("GPL");
