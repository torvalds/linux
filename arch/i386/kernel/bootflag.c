/*
 *	Implement 'Simple Boot Flag Specification 2.0'
 */


#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/acpi.h>
#include <asm/io.h>

#include <linux/mc146818rtc.h>


#define SBF_RESERVED (0x78)
#define SBF_PNPOS    (1<<0)
#define SBF_BOOTING  (1<<1)
#define SBF_DIAG     (1<<2)
#define SBF_PARITY   (1<<7)


int sbf_port __initdata = -1;	/* set via acpi_boot_init() */


static int __init parity(u8 v)
{
	int x = 0;
	int i;
	
	for(i=0;i<8;i++)
	{
		x^=(v&1);
		v>>=1;
	}
	return x;
}

static void __init sbf_write(u8 v)
{
	unsigned long flags;
	if(sbf_port != -1)
	{
		v &= ~SBF_PARITY;
		if(!parity(v))
			v|=SBF_PARITY;

		printk(KERN_INFO "Simple Boot Flag at 0x%x set to 0x%x\n", sbf_port, v);

		spin_lock_irqsave(&rtc_lock, flags);
		CMOS_WRITE(v, sbf_port);
		spin_unlock_irqrestore(&rtc_lock, flags);
	}
}

static u8 __init sbf_read(void)
{
	u8 v;
	unsigned long flags;
	if(sbf_port == -1)
		return 0;
	spin_lock_irqsave(&rtc_lock, flags);
	v = CMOS_READ(sbf_port);
	spin_unlock_irqrestore(&rtc_lock, flags);
	return v;
}

static int __init sbf_value_valid(u8 v)
{
	if(v&SBF_RESERVED)		/* Reserved bits */
		return 0;
	if(!parity(v))
		return 0;
	return 1;
}

static int __init sbf_init(void)
{
	u8 v;
	if(sbf_port == -1)
		return 0;
	v = sbf_read();
	if(!sbf_value_valid(v))
		printk(KERN_WARNING "Simple Boot Flag value 0x%x read from CMOS RAM was invalid\n",v);

	v &= ~SBF_RESERVED;
	v &= ~SBF_BOOTING;
	v &= ~SBF_DIAG;
#if defined(CONFIG_ISAPNP)
	v |= SBF_PNPOS;
#endif
	sbf_write(v);
	return 0;
}

module_init(sbf_init);
