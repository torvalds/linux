#ifndef MMC_MACKEREL_H
#define MMC_MACKEREL_H

#define PORT0CR      (void __iomem *)0xe6051000
#define PORT1CR      (void __iomem *)0xe6051001
#define PORT2CR      (void __iomem *)0xe6051002
#define PORT159CR    (void __iomem *)0xe605009f

#define PORTR031_000DR (void __iomem *)0xe6055000
#define PORTL159_128DR (void __iomem *)0xe6054010

static inline void mmc_init_progress(void)
{
       /* Initialise LEDS0-3
        * registers: PORT0CR-PORT2CR,PORT159CR (LED0-LED3 Control)
        * value:     0x10 - enable output
        */
       __raw_writeb(0x10, PORT0CR);
       __raw_writeb(0x10, PORT1CR);
       __raw_writeb(0x10, PORT2CR);
       __raw_writeb(0x10, PORT159CR);
}

static inline void mmc_update_progress(int n)
{
	unsigned a = 0, b = 0;

	if (n < 3)
		a = 1 << n;
	else
		b = 1 << 31;

	__raw_writel((__raw_readl(PORTR031_000DR) & ~0x7) | a,
		     PORTR031_000DR);
	__raw_writel((__raw_readl(PORTL159_128DR) & ~(1 << 31)) | b,
		     PORTL159_128DR);
}
#endif /* MMC_MACKEREL_H */
