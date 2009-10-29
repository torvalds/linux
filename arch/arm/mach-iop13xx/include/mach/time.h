#ifndef _IOP13XX_TIME_H_
#define _IOP13XX_TIME_H_
#define IRQ_IOP_TIMER0 IRQ_IOP13XX_TIMER0

#define IOP_TMR_EN	    0x02
#define IOP_TMR_RELOAD	    0x04
#define IOP_TMR_PRIVILEGED 0x08
#define IOP_TMR_RATIO_1_1  0x00

#define IOP13XX_XSI_FREQ_RATIO_MASK	(3 << 19)
#define IOP13XX_XSI_FREQ_RATIO_2   	(0 << 19)
#define IOP13XX_XSI_FREQ_RATIO_3	(1 << 19)
#define IOP13XX_XSI_FREQ_RATIO_4	(2 << 19)
#define IOP13XX_CORE_FREQ_MASK		(7 << 16)
#define IOP13XX_CORE_FREQ_600		(0 << 16)
#define IOP13XX_CORE_FREQ_667		(1 << 16)
#define IOP13XX_CORE_FREQ_800		(2 << 16)
#define IOP13XX_CORE_FREQ_933		(3 << 16)
#define IOP13XX_CORE_FREQ_1000		(4 << 16)
#define IOP13XX_CORE_FREQ_1200		(5 << 16)

void iop_init_time(unsigned long tickrate);
unsigned long iop_gettimeoffset(void);

static inline unsigned long iop13xx_core_freq(void)
{
	unsigned long freq = __raw_readl(IOP13XX_PROCESSOR_FREQ);
	freq &= IOP13XX_CORE_FREQ_MASK;
	switch (freq) {
	case IOP13XX_CORE_FREQ_600:
		return 600000000;
	case IOP13XX_CORE_FREQ_667:
		return 667000000;
	case IOP13XX_CORE_FREQ_800:
		return 800000000;
	case IOP13XX_CORE_FREQ_933:
		return 933000000;
	case IOP13XX_CORE_FREQ_1000:
		return 1000000000;
	case IOP13XX_CORE_FREQ_1200:
		return 1200000000;
	default:
		printk("%s: warning unknown frequency, defaulting to 800Mhz\n",
			__func__);
	}

	return 800000000;
}

static inline unsigned long iop13xx_xsi_bus_ratio(void)
{
	unsigned long  ratio = __raw_readl(IOP13XX_PROCESSOR_FREQ);
	ratio &= IOP13XX_XSI_FREQ_RATIO_MASK;
	switch (ratio) {
	case IOP13XX_XSI_FREQ_RATIO_2:
		return 2;
	case IOP13XX_XSI_FREQ_RATIO_3:
		return 3;
	case IOP13XX_XSI_FREQ_RATIO_4:
		return 4;
	default:
		printk("%s: warning unknown ratio, defaulting to 2\n",
			__func__);
	}

	return 2;
}

static inline void write_tmr0(u32 val)
{
	asm volatile("mcr p6, 0, %0, c0, c9, 0" : : "r" (val));
}

static inline void write_tmr1(u32 val)
{
	asm volatile("mcr p6, 0, %0, c1, c9, 0" : : "r" (val));
}

static inline u32 read_tcr0(void)
{
	u32 val;
	asm volatile("mrc p6, 0, %0, c2, c9, 0" : "=r" (val));
	return val;
}

static inline u32 read_tcr1(void)
{
	u32 val;
	asm volatile("mrc p6, 0, %0, c3, c9, 0" : "=r" (val));
	return val;
}

static inline void write_tcr1(u32 val)
{
	asm volatile("mcr p6, 0, %0, c3, c9, 0" : : "r" (val));
}

static inline void write_trr0(u32 val)
{
	asm volatile("mcr p6, 0, %0, c4, c9, 0" : : "r" (val));
}

static inline void write_trr1(u32 val)
{
	asm volatile("mcr p6, 0, %0, c5, c9, 0" : : "r" (val));
}

static inline void write_tisr(u32 val)
{
	asm volatile("mcr p6, 0, %0, c6, c9, 0" : : "r" (val));
}
#endif
