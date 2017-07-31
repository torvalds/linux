#include "tw5864.h"

void tw5864_indir_writeb(struct tw5864_dev *dev, u16 addr, u8 data)
{
	int retries = 30000;

	while (tw_readl(TW5864_IND_CTL) & BIT(31) && --retries)
		;
	if (!retries)
		dev_err(&dev->pci->dev,
			"tw_indir_writel() retries exhausted before writing\n");

	tw_writel(TW5864_IND_DATA, data);
	tw_writel(TW5864_IND_CTL, addr << 2 | TW5864_RW | TW5864_ENABLE);
}

u8 tw5864_indir_readb(struct tw5864_dev *dev, u16 addr)
{
	int retries = 30000;

	while (tw_readl(TW5864_IND_CTL) & BIT(31) && --retries)
		;
	if (!retries)
		dev_err(&dev->pci->dev,
			"tw_indir_readl() retries exhausted before reading\n");

	tw_writel(TW5864_IND_CTL, addr << 2 | TW5864_ENABLE);

	retries = 30000;
	while (tw_readl(TW5864_IND_CTL) & BIT(31) && --retries)
		;
	if (!retries)
		dev_err(&dev->pci->dev,
			"tw_indir_readl() retries exhausted at reading\n");

	return tw_readl(TW5864_IND_DATA);
}
