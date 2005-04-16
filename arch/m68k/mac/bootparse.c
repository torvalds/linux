#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/irq.h>
#include <asm/setup.h>
#include <asm/bootinfo.h>
#include <asm/macintosh.h>

/*
 *	Booter vars
 */

int boothowto;
int _boothowto;

/*
 *	Called early to parse the environment (passed to us from the booter)
 *	into a bootinfo struct. Will die as soon as we have our own booter
 */

#define atol(x)	simple_strtoul(x,NULL,0)

void parse_booter(char *env)
{
	char *name;
	char *value;
#if 0
	while(0 && *env)
#else
	while(*env)
#endif
	{
		name=env;
		value=name;
		while(*value!='='&&*value)
			value++;
		if(*value=='=')
			*value++=0;
		env=value;
		while(*env)
			env++;
		env++;
#if 0
		if(strcmp(name,"VIDEO_ADDR")==0)
			mac_mch.videoaddr=atol(value);
		if(strcmp(name,"ROW_BYTES")==0)
			mac_mch.videorow=atol(value);
		if(strcmp(name,"SCREEN_DEPTH")==0)
			mac_mch.videodepth=atol(value);
		if(strcmp(name,"DIMENSIONS")==0)
			mac_mch.dimensions=atol(value);
#endif
		if(strcmp(name,"BOOTTIME")==0)
			mac_bi_data.boottime=atol(value);
		if(strcmp(name,"GMTBIAS")==0)
			mac_bi_data.gmtbias=atol(value);
		if(strcmp(name,"BOOTERVER")==0)
			mac_bi_data.bootver=atol(value);
		if(strcmp(name,"MACOS_VIDEO")==0)
			mac_bi_data.videological=atol(value);
		if(strcmp(name,"MACOS_SCC")==0)
			mac_bi_data.sccbase=atol(value);
		if(strcmp(name,"MACHINEID")==0)
			mac_bi_data.id=atol(value);
		if(strcmp(name,"MEMSIZE")==0)
			mac_bi_data.memsize=atol(value);
		if(strcmp(name,"SERIAL_MODEM_FLAGS")==0)
			mac_bi_data.serialmf=atol(value);
		if(strcmp(name,"SERIAL_MODEM_HSKICLK")==0)
			mac_bi_data.serialhsk=atol(value);
		if(strcmp(name,"SERIAL_MODEM_GPICLK")==0)
			mac_bi_data.serialgpi=atol(value);
		if(strcmp(name,"SERIAL_PRINT_FLAGS")==0)
			mac_bi_data.printmf=atol(value);
		if(strcmp(name,"SERIAL_PRINT_HSKICLK")==0)
			mac_bi_data.printhsk=atol(value);
		if(strcmp(name,"SERIAL_PRINT_GPICLK")==0)
			mac_bi_data.printgpi=atol(value);
		if(strcmp(name,"PROCESSOR")==0)
			mac_bi_data.cpuid=atol(value);
		if(strcmp(name,"ROMBASE")==0)
			mac_bi_data.rombase=atol(value);
		if(strcmp(name,"TIMEDBRA")==0)
			mac_bi_data.timedbra=atol(value);
		if(strcmp(name,"ADBDELAY")==0)
			mac_bi_data.adbdelay=atol(value);
	}
#if 0	/* XXX: TODO with m68k_mach_* */
	/* Fill in the base stuff */
	boot_info.machtype=MACH_MAC;
	/* Read this from the macinfo we got ! */
/*	boot_info.cputype=CPU_68020|FPUB_68881;*/
/*	boot_info.memory[0].addr=0;*/
/*	boot_info.memory[0].size=((mac_bi_data.id>>7)&31)<<20;*/
	boot_info.num_memory=1;		/* On a MacII */
	boot_info.ramdisk_size=0;	/* For now */
	*boot_info.command_line=0;
#endif
 }


void print_booter(char *env)
{
	char *name;
	char *value;
	while(*env)
	{
		name=env;
		value=name;
		while(*value!='='&&*value)
			value++;
		if(*value=='=')
			*value++=0;
		env=value;
		while(*env)
			env++;
		env++;
		printk("%s=%s\n", name,value);
	}
 }


