#ifndef BCM63XX_CPU_H_
#define BCM63XX_CPU_H_

#include <linux/types.h>
#include <linux/init.h>

/*
 * Macro to fetch bcm63xx cpu id and revision, should be optimized at
 * compile time if only one CPU support is enabled (idea stolen from
 * arm mach-types)
 */
#define BCM6338_CPU_ID		0x6338
#define BCM6345_CPU_ID		0x6345
#define BCM6348_CPU_ID		0x6348
#define BCM6358_CPU_ID		0x6358

void __init bcm63xx_cpu_init(void);
u16 __bcm63xx_get_cpu_id(void);
u16 bcm63xx_get_cpu_rev(void);
unsigned int bcm63xx_get_cpu_freq(void);

#ifdef CONFIG_BCM63XX_CPU_6338
# ifdef bcm63xx_get_cpu_id
#  undef bcm63xx_get_cpu_id
#  define bcm63xx_get_cpu_id()	__bcm63xx_get_cpu_id()
#  define BCMCPU_RUNTIME_DETECT
# else
#  define bcm63xx_get_cpu_id()	BCM6338_CPU_ID
# endif
# define BCMCPU_IS_6338()	(bcm63xx_get_cpu_id() == BCM6338_CPU_ID)
#else
# define BCMCPU_IS_6338()	(0)
#endif

#ifdef CONFIG_BCM63XX_CPU_6345
# ifdef bcm63xx_get_cpu_id
#  undef bcm63xx_get_cpu_id
#  define bcm63xx_get_cpu_id()	__bcm63xx_get_cpu_id()
#  define BCMCPU_RUNTIME_DETECT
# else
#  define bcm63xx_get_cpu_id()	BCM6345_CPU_ID
# endif
# define BCMCPU_IS_6345()	(bcm63xx_get_cpu_id() == BCM6345_CPU_ID)
#else
# define BCMCPU_IS_6345()	(0)
#endif

#ifdef CONFIG_BCM63XX_CPU_6348
# ifdef bcm63xx_get_cpu_id
#  undef bcm63xx_get_cpu_id
#  define bcm63xx_get_cpu_id()	__bcm63xx_get_cpu_id()
#  define BCMCPU_RUNTIME_DETECT
# else
#  define bcm63xx_get_cpu_id()	BCM6348_CPU_ID
# endif
# define BCMCPU_IS_6348()	(bcm63xx_get_cpu_id() == BCM6348_CPU_ID)
#else
# define BCMCPU_IS_6348()	(0)
#endif

#ifdef CONFIG_BCM63XX_CPU_6358
# ifdef bcm63xx_get_cpu_id
#  undef bcm63xx_get_cpu_id
#  define bcm63xx_get_cpu_id()	__bcm63xx_get_cpu_id()
#  define BCMCPU_RUNTIME_DETECT
# else
#  define bcm63xx_get_cpu_id()	BCM6358_CPU_ID
# endif
# define BCMCPU_IS_6358()	(bcm63xx_get_cpu_id() == BCM6358_CPU_ID)
#else
# define BCMCPU_IS_6358()	(0)
#endif

#ifndef bcm63xx_get_cpu_id
#error "No CPU support configured"
#endif

/*
 * While registers sets are (mostly) the same across 63xx CPU, base
 * address of these sets do change.
 */
enum bcm63xx_regs_set {
	RSET_DSL_LMEM = 0,
	RSET_PERF,
	RSET_TIMER,
	RSET_WDT,
	RSET_UART0,
	RSET_UART1,
	RSET_GPIO,
	RSET_SPI,
	RSET_UDC0,
	RSET_OHCI0,
	RSET_OHCI_PRIV,
	RSET_USBH_PRIV,
	RSET_MPI,
	RSET_PCMCIA,
	RSET_DSL,
	RSET_ENET0,
	RSET_ENET1,
	RSET_ENETDMA,
	RSET_EHCI0,
	RSET_SDRAM,
	RSET_MEMC,
	RSET_DDR,
};

#define RSET_DSL_LMEM_SIZE		(64 * 1024 * 4)
#define RSET_DSL_SIZE			4096
#define RSET_WDT_SIZE			12
#define RSET_ENET_SIZE			2048
#define RSET_ENETDMA_SIZE		2048
#define RSET_UART_SIZE			24
#define RSET_UDC_SIZE			256
#define RSET_OHCI_SIZE			256
#define RSET_EHCI_SIZE			256
#define RSET_PCMCIA_SIZE		12

/*
 * 6338 register sets base address
 */
#define BCM_6338_DSL_LMEM_BASE		(0xfff00000)
#define BCM_6338_PERF_BASE		(0xfffe0000)
#define BCM_6338_BB_BASE		(0xfffe0100)
#define BCM_6338_TIMER_BASE		(0xfffe0200)
#define BCM_6338_WDT_BASE		(0xfffe021c)
#define BCM_6338_UART0_BASE		(0xfffe0300)
#define BCM_6338_UART1_BASE		(0xdeadbeef)
#define BCM_6338_GPIO_BASE		(0xfffe0400)
#define BCM_6338_SPI_BASE		(0xfffe0c00)
#define BCM_6338_UDC0_BASE		(0xdeadbeef)
#define BCM_6338_USBDMA_BASE		(0xfffe2400)
#define BCM_6338_OHCI0_BASE		(0xdeadbeef)
#define BCM_6338_OHCI_PRIV_BASE		(0xfffe3000)
#define BCM_6338_USBH_PRIV_BASE		(0xdeadbeef)
#define BCM_6338_MPI_BASE		(0xfffe3160)
#define BCM_6338_PCMCIA_BASE		(0xdeadbeef)
#define BCM_6338_SDRAM_REGS_BASE	(0xfffe3100)
#define BCM_6338_DSL_BASE		(0xfffe1000)
#define BCM_6338_SAR_BASE		(0xfffe2000)
#define BCM_6338_UBUS_BASE		(0xdeadbeef)
#define BCM_6338_ENET0_BASE		(0xfffe2800)
#define BCM_6338_ENET1_BASE		(0xdeadbeef)
#define BCM_6338_ENETDMA_BASE		(0xfffe2400)
#define BCM_6338_EHCI0_BASE		(0xdeadbeef)
#define BCM_6338_SDRAM_BASE		(0xfffe3100)
#define BCM_6338_MEMC_BASE		(0xdeadbeef)
#define BCM_6338_DDR_BASE		(0xdeadbeef)

/*
 * 6345 register sets base address
 */
#define BCM_6345_DSL_LMEM_BASE		(0xfff00000)
#define BCM_6345_PERF_BASE		(0xfffe0000)
#define BCM_6345_BB_BASE		(0xfffe0100)
#define BCM_6345_TIMER_BASE		(0xfffe0200)
#define BCM_6345_WDT_BASE		(0xfffe021c)
#define BCM_6345_UART0_BASE		(0xfffe0300)
#define BCM_6345_UART1_BASE		(0xdeadbeef)
#define BCM_6345_GPIO_BASE		(0xfffe0400)
#define BCM_6345_SPI_BASE		(0xdeadbeef)
#define BCM_6345_UDC0_BASE		(0xdeadbeef)
#define BCM_6345_USBDMA_BASE		(0xfffe2800)
#define BCM_6345_ENET0_BASE		(0xfffe1800)
#define BCM_6345_ENETDMA_BASE		(0xfffe2800)
#define BCM_6345_PCMCIA_BASE		(0xfffe2028)
#define BCM_6345_MPI_BASE		(0xdeadbeef)
#define BCM_6345_OHCI0_BASE		(0xfffe2100)
#define BCM_6345_OHCI_PRIV_BASE		(0xfffe2200)
#define BCM_6345_USBH_PRIV_BASE		(0xdeadbeef)
#define BCM_6345_SDRAM_REGS_BASE	(0xfffe2300)
#define BCM_6345_DSL_BASE		(0xdeadbeef)
#define BCM_6345_SAR_BASE		(0xdeadbeef)
#define BCM_6345_UBUS_BASE		(0xdeadbeef)
#define BCM_6345_ENET1_BASE		(0xdeadbeef)
#define BCM_6345_EHCI0_BASE		(0xdeadbeef)
#define BCM_6345_SDRAM_BASE		(0xfffe2300)
#define BCM_6345_MEMC_BASE		(0xdeadbeef)
#define BCM_6345_DDR_BASE		(0xdeadbeef)

/*
 * 6348 register sets base address
 */
#define BCM_6348_DSL_LMEM_BASE		(0xfff00000)
#define BCM_6348_PERF_BASE		(0xfffe0000)
#define BCM_6348_TIMER_BASE		(0xfffe0200)
#define BCM_6348_WDT_BASE		(0xfffe021c)
#define BCM_6348_UART0_BASE		(0xfffe0300)
#define BCM_6348_UART1_BASE		(0xdeadbeef)
#define BCM_6348_GPIO_BASE		(0xfffe0400)
#define BCM_6348_SPI_BASE		(0xfffe0c00)
#define BCM_6348_UDC0_BASE		(0xfffe1000)
#define BCM_6348_OHCI0_BASE		(0xfffe1b00)
#define BCM_6348_OHCI_PRIV_BASE		(0xfffe1c00)
#define BCM_6348_USBH_PRIV_BASE		(0xdeadbeef)
#define BCM_6348_MPI_BASE		(0xfffe2000)
#define BCM_6348_PCMCIA_BASE		(0xfffe2054)
#define BCM_6348_SDRAM_REGS_BASE	(0xfffe2300)
#define BCM_6348_DSL_BASE		(0xfffe3000)
#define BCM_6348_ENET0_BASE		(0xfffe6000)
#define BCM_6348_ENET1_BASE		(0xfffe6800)
#define BCM_6348_ENETDMA_BASE		(0xfffe7000)
#define BCM_6348_EHCI0_BASE		(0xdeadbeef)
#define BCM_6348_SDRAM_BASE		(0xfffe2300)
#define BCM_6348_MEMC_BASE		(0xdeadbeef)
#define BCM_6348_DDR_BASE		(0xdeadbeef)

/*
 * 6358 register sets base address
 */
#define BCM_6358_DSL_LMEM_BASE		(0xfff00000)
#define BCM_6358_PERF_BASE		(0xfffe0000)
#define BCM_6358_TIMER_BASE		(0xfffe0040)
#define BCM_6358_WDT_BASE		(0xfffe005c)
#define BCM_6358_UART0_BASE		(0xfffe0100)
#define BCM_6358_UART1_BASE		(0xfffe0120)
#define BCM_6358_GPIO_BASE		(0xfffe0080)
#define BCM_6358_SPI_BASE		(0xdeadbeef)
#define BCM_6358_UDC0_BASE		(0xfffe0800)
#define BCM_6358_OHCI0_BASE		(0xfffe1400)
#define BCM_6358_OHCI_PRIV_BASE		(0xdeadbeef)
#define BCM_6358_USBH_PRIV_BASE		(0xfffe1500)
#define BCM_6358_MPI_BASE		(0xfffe1000)
#define BCM_6358_PCMCIA_BASE		(0xfffe1054)
#define BCM_6358_SDRAM_REGS_BASE	(0xfffe2300)
#define BCM_6358_DSL_BASE		(0xfffe3000)
#define BCM_6358_ENET0_BASE		(0xfffe4000)
#define BCM_6358_ENET1_BASE		(0xfffe4800)
#define BCM_6358_ENETDMA_BASE		(0xfffe5000)
#define BCM_6358_EHCI0_BASE		(0xfffe1300)
#define BCM_6358_SDRAM_BASE		(0xdeadbeef)
#define BCM_6358_MEMC_BASE		(0xfffe1200)
#define BCM_6358_DDR_BASE		(0xfffe12a0)


extern const unsigned long *bcm63xx_regs_base;

#define __GEN_RSET_BASE(__cpu, __rset)					\
	case RSET_## __rset :						\
		return BCM_## __cpu ##_## __rset ##_BASE;

#define __GEN_RSET(__cpu)						\
	switch (set) {							\
	__GEN_RSET_BASE(__cpu, DSL_LMEM)				\
	__GEN_RSET_BASE(__cpu, PERF)					\
	__GEN_RSET_BASE(__cpu, TIMER)					\
	__GEN_RSET_BASE(__cpu, WDT)					\
	__GEN_RSET_BASE(__cpu, UART0)					\
	__GEN_RSET_BASE(__cpu, UART1)					\
	__GEN_RSET_BASE(__cpu, GPIO)					\
	__GEN_RSET_BASE(__cpu, SPI)					\
	__GEN_RSET_BASE(__cpu, UDC0)					\
	__GEN_RSET_BASE(__cpu, OHCI0)					\
	__GEN_RSET_BASE(__cpu, OHCI_PRIV)				\
	__GEN_RSET_BASE(__cpu, USBH_PRIV)				\
	__GEN_RSET_BASE(__cpu, MPI)					\
	__GEN_RSET_BASE(__cpu, PCMCIA)					\
	__GEN_RSET_BASE(__cpu, DSL)					\
	__GEN_RSET_BASE(__cpu, ENET0)					\
	__GEN_RSET_BASE(__cpu, ENET1)					\
	__GEN_RSET_BASE(__cpu, ENETDMA)					\
	__GEN_RSET_BASE(__cpu, EHCI0)					\
	__GEN_RSET_BASE(__cpu, SDRAM)					\
	__GEN_RSET_BASE(__cpu, MEMC)					\
	__GEN_RSET_BASE(__cpu, DDR)					\
	}

#define __GEN_CPU_REGS_TABLE(__cpu)					\
	[RSET_DSL_LMEM]		= BCM_## __cpu ##_DSL_LMEM_BASE,	\
	[RSET_PERF]		= BCM_## __cpu ##_PERF_BASE,		\
	[RSET_TIMER]		= BCM_## __cpu ##_TIMER_BASE,		\
	[RSET_WDT]		= BCM_## __cpu ##_WDT_BASE,		\
	[RSET_UART0]		= BCM_## __cpu ##_UART0_BASE,		\
	[RSET_UART1]		= BCM_## __cpu ##_UART1_BASE,		\
	[RSET_GPIO]		= BCM_## __cpu ##_GPIO_BASE,		\
	[RSET_SPI]		= BCM_## __cpu ##_SPI_BASE,		\
	[RSET_UDC0]		= BCM_## __cpu ##_UDC0_BASE,		\
	[RSET_OHCI0]		= BCM_## __cpu ##_OHCI0_BASE,		\
	[RSET_OHCI_PRIV]	= BCM_## __cpu ##_OHCI_PRIV_BASE,	\
	[RSET_USBH_PRIV]	= BCM_## __cpu ##_USBH_PRIV_BASE,	\
	[RSET_MPI]		= BCM_## __cpu ##_MPI_BASE,		\
	[RSET_PCMCIA]		= BCM_## __cpu ##_PCMCIA_BASE,		\
	[RSET_DSL]		= BCM_## __cpu ##_DSL_BASE,		\
	[RSET_ENET0]		= BCM_## __cpu ##_ENET0_BASE,		\
	[RSET_ENET1]		= BCM_## __cpu ##_ENET1_BASE,		\
	[RSET_ENETDMA]		= BCM_## __cpu ##_ENETDMA_BASE,		\
	[RSET_EHCI0]		= BCM_## __cpu ##_EHCI0_BASE,		\
	[RSET_SDRAM]		= BCM_## __cpu ##_SDRAM_BASE,		\
	[RSET_MEMC]		= BCM_## __cpu ##_MEMC_BASE,		\
	[RSET_DDR]		= BCM_## __cpu ##_DDR_BASE,		\


static inline unsigned long bcm63xx_regset_address(enum bcm63xx_regs_set set)
{
#ifdef BCMCPU_RUNTIME_DETECT
	return bcm63xx_regs_base[set];
#else
#ifdef CONFIG_BCM63XX_CPU_6338
	__GEN_RSET(6338)
#endif
#ifdef CONFIG_BCM63XX_CPU_6345
	__GEN_RSET(6345)
#endif
#ifdef CONFIG_BCM63XX_CPU_6348
	__GEN_RSET(6348)
#endif
#ifdef CONFIG_BCM63XX_CPU_6358
	__GEN_RSET(6358)
#endif
#endif
	/* unreached */
	return 0;
}

/*
 * IRQ number changes across CPU too
 */
enum bcm63xx_irq {
	IRQ_TIMER = 0,
	IRQ_UART0,
	IRQ_UART1,
	IRQ_DSL,
	IRQ_ENET0,
	IRQ_ENET1,
	IRQ_ENET_PHY,
	IRQ_OHCI0,
	IRQ_EHCI0,
	IRQ_ENET0_RXDMA,
	IRQ_ENET0_TXDMA,
	IRQ_ENET1_RXDMA,
	IRQ_ENET1_TXDMA,
	IRQ_PCI,
	IRQ_PCMCIA,
};

/*
 * 6338 irqs
 */
#define BCM_6338_TIMER_IRQ		(IRQ_INTERNAL_BASE + 0)
#define BCM_6338_UART0_IRQ		(IRQ_INTERNAL_BASE + 2)
#define BCM_6338_UART1_IRQ		0
#define BCM_6338_DSL_IRQ		(IRQ_INTERNAL_BASE + 5)
#define BCM_6338_ENET0_IRQ		(IRQ_INTERNAL_BASE + 8)
#define BCM_6338_ENET1_IRQ		0
#define BCM_6338_ENET_PHY_IRQ		(IRQ_INTERNAL_BASE + 9)
#define BCM_6338_OHCI0_IRQ		0
#define BCM_6338_EHCI0_IRQ		0
#define BCM_6338_ENET0_RXDMA_IRQ	(IRQ_INTERNAL_BASE + 15)
#define BCM_6338_ENET0_TXDMA_IRQ	(IRQ_INTERNAL_BASE + 16)
#define BCM_6338_ENET1_RXDMA_IRQ	0
#define BCM_6338_ENET1_TXDMA_IRQ	0
#define BCM_6338_PCI_IRQ		0
#define BCM_6338_PCMCIA_IRQ		0

/*
 * 6345 irqs
 */
#define BCM_6345_TIMER_IRQ		(IRQ_INTERNAL_BASE + 0)
#define BCM_6345_UART0_IRQ		(IRQ_INTERNAL_BASE + 2)
#define BCM_6345_UART1_IRQ		0
#define BCM_6345_DSL_IRQ		(IRQ_INTERNAL_BASE + 3)
#define BCM_6345_ENET0_IRQ		(IRQ_INTERNAL_BASE + 8)
#define BCM_6345_ENET1_IRQ		0
#define BCM_6345_ENET_PHY_IRQ		(IRQ_INTERNAL_BASE + 12)
#define BCM_6345_OHCI0_IRQ		0
#define BCM_6345_EHCI0_IRQ		0
#define BCM_6345_ENET0_RXDMA_IRQ	(IRQ_INTERNAL_BASE + 13 + 1)
#define BCM_6345_ENET0_TXDMA_IRQ	(IRQ_INTERNAL_BASE + 13 + 2)
#define BCM_6345_ENET1_RXDMA_IRQ	0
#define BCM_6345_ENET1_TXDMA_IRQ	0
#define BCM_6345_PCI_IRQ		0
#define BCM_6345_PCMCIA_IRQ		0

/*
 * 6348 irqs
 */
#define BCM_6348_TIMER_IRQ		(IRQ_INTERNAL_BASE + 0)
#define BCM_6348_UART0_IRQ		(IRQ_INTERNAL_BASE + 2)
#define BCM_6348_UART1_IRQ		0
#define BCM_6348_DSL_IRQ		(IRQ_INTERNAL_BASE + 4)
#define BCM_6348_ENET0_IRQ		(IRQ_INTERNAL_BASE + 8)
#define BCM_6348_ENET1_IRQ		(IRQ_INTERNAL_BASE + 7)
#define BCM_6348_ENET_PHY_IRQ		(IRQ_INTERNAL_BASE + 9)
#define BCM_6348_OHCI0_IRQ		(IRQ_INTERNAL_BASE + 12)
#define BCM_6348_EHCI0_IRQ		0
#define BCM_6348_ENET0_RXDMA_IRQ	(IRQ_INTERNAL_BASE + 20)
#define BCM_6348_ENET0_TXDMA_IRQ	(IRQ_INTERNAL_BASE + 21)
#define BCM_6348_ENET1_RXDMA_IRQ	(IRQ_INTERNAL_BASE + 22)
#define BCM_6348_ENET1_TXDMA_IRQ	(IRQ_INTERNAL_BASE + 23)
#define BCM_6348_PCI_IRQ		(IRQ_INTERNAL_BASE + 24)
#define BCM_6348_PCMCIA_IRQ		(IRQ_INTERNAL_BASE + 24)

/*
 * 6358 irqs
 */
#define BCM_6358_TIMER_IRQ		(IRQ_INTERNAL_BASE + 0)
#define BCM_6358_UART0_IRQ		(IRQ_INTERNAL_BASE + 2)
#define BCM_6358_UART1_IRQ		(IRQ_INTERNAL_BASE + 3)
#define BCM_6358_DSL_IRQ		(IRQ_INTERNAL_BASE + 29)
#define BCM_6358_ENET0_IRQ		(IRQ_INTERNAL_BASE + 8)
#define BCM_6358_ENET1_IRQ		(IRQ_INTERNAL_BASE + 6)
#define BCM_6358_ENET_PHY_IRQ		(IRQ_INTERNAL_BASE + 9)
#define BCM_6358_OHCI0_IRQ		(IRQ_INTERNAL_BASE + 5)
#define BCM_6358_EHCI0_IRQ		(IRQ_INTERNAL_BASE + 10)
#define BCM_6358_ENET0_RXDMA_IRQ	(IRQ_INTERNAL_BASE + 15)
#define BCM_6358_ENET0_TXDMA_IRQ	(IRQ_INTERNAL_BASE + 16)
#define BCM_6358_ENET1_RXDMA_IRQ	(IRQ_INTERNAL_BASE + 17)
#define BCM_6358_ENET1_TXDMA_IRQ	(IRQ_INTERNAL_BASE + 18)
#define BCM_6358_PCI_IRQ		(IRQ_INTERNAL_BASE + 31)
#define BCM_6358_PCMCIA_IRQ		(IRQ_INTERNAL_BASE + 24)

extern const int *bcm63xx_irqs;

#define __GEN_CPU_IRQ_TABLE(__cpu)					\
	[IRQ_TIMER]		= BCM_## __cpu ##_TIMER_IRQ,		\
	[IRQ_UART0]		= BCM_## __cpu ##_UART0_IRQ,		\
	[IRQ_UART1]		= BCM_## __cpu ##_UART1_IRQ,		\
	[IRQ_DSL]		= BCM_## __cpu ##_DSL_IRQ,		\
	[IRQ_ENET0]		= BCM_## __cpu ##_ENET0_IRQ,		\
	[IRQ_ENET1]		= BCM_## __cpu ##_ENET1_IRQ,		\
	[IRQ_ENET_PHY]		= BCM_## __cpu ##_ENET_PHY_IRQ,		\
	[IRQ_OHCI0]		= BCM_## __cpu ##_OHCI0_IRQ,		\
	[IRQ_EHCI0]		= BCM_## __cpu ##_EHCI0_IRQ,		\
	[IRQ_ENET0_RXDMA]	= BCM_## __cpu ##_ENET0_RXDMA_IRQ,	\
	[IRQ_ENET0_TXDMA]	= BCM_## __cpu ##_ENET0_TXDMA_IRQ,	\
	[IRQ_ENET1_RXDMA]	= BCM_## __cpu ##_ENET1_RXDMA_IRQ,	\
	[IRQ_ENET1_TXDMA]	= BCM_## __cpu ##_ENET1_TXDMA_IRQ,	\
	[IRQ_PCI]		= BCM_## __cpu ##_PCI_IRQ,		\
	[IRQ_PCMCIA]		= BCM_## __cpu ##_PCMCIA_IRQ,		\

static inline int bcm63xx_get_irq_number(enum bcm63xx_irq irq)
{
	return bcm63xx_irqs[irq];
}

/*
 * return installed memory size
 */
unsigned int bcm63xx_get_memory_size(void);

#endif /* !BCM63XX_CPU_H_ */
