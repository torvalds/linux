/*
 ******************************************************************************
 * @file  starfive-trng.c
 * @author  StarFive Technology
 * @version  V1.0
 * @date  09/08/2021
 * @brief
 ******************************************************************************
 * @copy
 *
 * THE PRESENT SOFTWARE WHICH IS FOR GUIDANCE ONLY AIMS AT PROVIDING CUSTOMERS
 * WITH CODING INFORMATION REGARDING THEIR PRODUCTS IN ORDER FOR THEM TO SAVE
 * TIME. AS A RESULT, STARFIVE SHALL NOT BE HELD LIABLE FOR ANY
 * DIRECT, INDIRECT OR CONSEQUENTIAL DAMAGES WITH RESPECT TO ANY CLAIMS ARISING
 * FROM THE CONTENT OF SUCH SOFTWARE AND/OR THE USE MADE BY CUSTOMERS OF THE
 * CODING INFORMATION CONTAINED HEREIN IN CONNECTION WITH THEIR PRODUCTS.
 *
 * COPYRIGHT 2020 Shanghai StarFive Technology Co., Ltd.
 */
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/hw_random.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/random.h>
#include <linux/delay.h>

#include "starfive-trng.h"


#define to_trng(p)	container_of(p, struct trng, rng)

struct trng {
	struct device	*dev;
	void __iomem	*base;
	u32	mode;
	u32	ctl_cmd;
	u32	test_mode;
	u32	reseed;
	u32	opmode;
	volatile int 	trng_reseed_done;
	volatile int 	trng_random_done;
	struct hwrng	rng;
};

static inline void trng_wait_till_idle(struct trng *hrng)
{
	while(readl(hrng->base + CCORE_STAT) & (CCORE_STAT_RAND_GENERATING|CCORE_STAT_RAND_SEEDING))
		;
}

static int trng_put_nonce(struct trng *hrng, const void *nonce_in, int len)
{
	memcpy_toio(hrng->base + CCORE_SEED0, nonce_in, len);
	return 0;
}


static inline int is_random_done(struct trng *hrng)
{
	u32 stat;

	if(hrng->opmode == poll_mode) {
		stat = readl(hrng->base + CCORE_STAT);
		if((stat & CCORE_STAT_RAND_GENERATING) != CCORE_STAT_RAND_GENERATING) {
			hrng->trng_random_done = 1;
		}
	}

	return (hrng->trng_random_done);
}

static inline int is_reseed_done(struct trng *hrng)
{
	u32 stat;

	if(hrng->opmode == poll_mode) {
		stat = readl(hrng->base + CCORE_STAT);
		if(stat & CCORE_STAT_SEEDED) {
			hrng->trng_reseed_done = 1;
		}
	}

	return (hrng->trng_reseed_done);
}


static inline void trng_irq_mask_clear(struct trng *hrng)
{
	// clear register: ISTAT
	u32 data = readl(hrng->base + CCORE_ISTAT);
	writel(data, hrng->base + CCORE_ISTAT);
}


static int trng_random_reseed(struct trng *hrng)
{
	writel(CCORE_CTRL_EXEC_RANDRESEED, hrng->base + CCORE_CTRL);

	do {
		mdelay(10);
	} while(!is_reseed_done(hrng));
	hrng->trng_reseed_done = 0;
	
	// start random
	writel(CCORE_CTRL_GENE_RANDOM, hrng->base + CCORE_CTRL);
	return 0;
}


static int trng_nonce_reseed(struct trng *hrng, const void *nonce_in, int len)
{
	writel(CCORE_CTRL_EXEC_NONCRESEED, hrng->base + CCORE_CTRL);
	trng_put_nonce(hrng, nonce_in, len);
	
	do {
		mdelay(10);
	} while(!is_reseed_done(hrng));
	hrng->trng_reseed_done = 0;
	
	// start random
	writel(CCORE_CTRL_GENE_RANDOM, hrng->base + CCORE_CTRL);
	return 0;
}

static int trng_cmd(struct trng *hrng, u32 cmd) {
	int res = 0;
	u32 trng_nonce[8] = {
		0xcefaedfe, 0xefbeadde, 0xcefaedfe, 0xefbeadde,
		0xd2daadab, 0x00000000, 0x00000000, 0x00000000,
	};

	// wait till idle
	trng_wait_till_idle(hrng);

	//start trng
	switch (cmd) {
	case CCORE_CTRL_EXEC_NOP:
	case CCORE_CTRL_GENE_RANDOM:
		writel(cmd, hrng->base + CCORE_CTRL);
		break;
	
	case CCORE_CTRL_EXEC_RANDRESEED:
		trng_random_reseed(hrng);
		break;
	
	case CCORE_CTRL_EXEC_NONCRESEED:
		trng_nonce_reseed(hrng, trng_nonce, sizeof(trng_nonce));
		break;
	default:
		res = -1;
		break;
	}

	return res;
}


static int trng_init(struct hwrng *rng)
{
	struct trng *hrng = to_trng(rng);
	u32 mode, smode = 0;

	//disable Auto Request/Age register
	writel(AUTOAGE_DISABLED, hrng->base + CCORE_AUTO_AGE);
	writel(AUTOREQ_DISABLED, hrng->base + CCORE_AUTO_RQSTS);

	// clear register: ISTAT
	trng_irq_mask_clear(hrng);

	//set smode/mode 
	mode  = readl(hrng->base + CCORE_MODE);
	smode = readl(hrng->base + CCORE_SMODE);

	switch(hrng->mode) {
	case PRNG_128BIT:
		mode &= ~CCORE_MODE_R256;
		break;
	case PRNG_256BIT:
		mode |= CCORE_MODE_R256;
		break;
	default:
		dev_info(hrng->dev, "Use Default mode PRNG_256BIT\r\n");
		mode |= CCORE_MODE_R256;
		break;
	}

	if(hrng->test_mode == 1) {
		smode |= CCORE_SMODE_MISSION_MODE;
	}

	if(hrng->reseed == NONCE_RESEED) {
		smode |= CCORE_SMODE_NONCE_MODE;
	}

	writel(mode, hrng->base + CCORE_MODE);
	writel(smode, hrng->base + CCORE_SMODE);

	//clear int_mode
	if(hrng->opmode == int_mode) {
		writel(0, hrng->base + CCORE_IE);
	}
	return 0;
}

static irqreturn_t trng_irq(int irq, void *priv)
{
	u32 status;
	struct trng *hrng = (struct trng *)priv;

	status = readl(hrng->base + CCORE_ISTAT);
	if (status & CCORE_ISTAT_RAND_RDY) {
		
		writel(CCORE_ISTAT_RAND_RDY, hrng->base + CCORE_ISTAT);
		//dev_info(hrng->dev, "rand ready\r\n");
		hrng->trng_random_done = 1;
	}

	if (status & CCORE_ISTAT_SEED_DONE) {
		
		writel(CCORE_ISTAT_SEED_DONE, hrng->base + CCORE_ISTAT);
		//dev_info(hrng->dev, "seed ready\r\n");
		hrng->trng_reseed_done = 1 ;
	}


	if (status & CCORE_ISTAT_AGE_ALARM) {
		
		writel(CCORE_ISTAT_AGE_ALARM, hrng->base + CCORE_ISTAT);
		//dev_info(hrng->dev, "age alarm\r\n");	
	}

	if (status & CCORE_ISTAT_LFSR_LOOKUP) {
		
		writel(CCORE_ISTAT_LFSR_LOOKUP, hrng->base + CCORE_ISTAT);
		//dev_info(hrng->dev, "lfsr lookup\r\n");
	}

	trng_irq_mask_clear(hrng);

	return IRQ_HANDLED;
}

static void trng_cleanup(struct hwrng *rng)
{
	struct trng *hrng = to_trng(rng);

	writel(0, hrng->base + CCORE_CTRL);
}


static int trng_read(struct hwrng *rng, void *buf, size_t max, bool wait)
{
	struct trng *hrng = to_trng(rng);
	u32 intr = 0;
	
	trng_cmd(hrng, CCORE_CTRL_EXEC_NOP);
	//trng_wait_till_idle(hrng);
		
	if(hrng->mode == PRNG_256BIT)
		max = min_t(size_t, max, (CCORE_RAND_LEN * 4));
	else
		max = min_t(size_t, max, (CCORE_RAND_LEN/2 * 4));
	
	hrng->trng_random_done = 0;
	if(hrng->opmode == int_mode) {
		intr |= CCORE_IE_ALL;
	}
	writel(intr, hrng->base + CCORE_IE);

	trng_cmd(hrng, hrng->ctl_cmd);
	//trng_wait_till_idle(hrng);

	if(wait)
	{
		do {
			mdelay(10);
		} while(!is_random_done(hrng));
	}
	
	if(is_random_done(hrng))
	{
		memcpy_fromio(buf, hrng->base + CCORE_RAND0, max);
	}
	else
		max = 0;

	trng_cmd(hrng, CCORE_CTRL_EXEC_NOP);	
	trng_wait_till_idle(hrng);
	writel(0, hrng->base + CCORE_IE);
	
	return max;
}

static int trng_probe(struct platform_device *pdev)
{
	int ret;
	int irq;
	struct trng *rng;
	struct resource *res;
		
	rng = devm_kzalloc(&pdev->dev, sizeof(*rng), GFP_KERNEL);
	if (!rng){
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, rng);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	rng->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(rng->base)){
		return PTR_ERR(rng->base);
	}

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		dev_err(&pdev->dev, "Couldn't get irq %d\n", irq);
		return irq;
	}

	ret = devm_request_irq(&pdev->dev, irq, trng_irq, 0, pdev->name,
				(void *)rng);
	if (ret) {
		dev_err(&pdev->dev, "Can't get interrupt working.\n");
		return ret;
	}

	rng->rng.name = pdev->name;
	rng->rng.init = trng_init;
	rng->rng.cleanup = trng_cleanup;
	rng->rng.read = trng_read;
	
	rng->mode = PRNG_256BIT;
	rng->ctl_cmd = CCORE_CTRL_EXEC_RANDRESEED;
	if(rng->ctl_cmd == CCORE_CTRL_EXEC_NONCRESEED)
	{
		rng->opmode = poll_mode;
		rng->test_mode = 1;
		rng->reseed = NONCE_RESEED;
	}
	else
	{
		rng->opmode = int_mode;
		rng->test_mode = 0;
		rng->reseed = RANDOM_RESEED;
	}
	rng->dev = &pdev->dev;

	ret = devm_hwrng_register(&pdev->dev, &rng->rng);
	if (ret) {
		dev_err(&pdev->dev, "failed to register hwrng\n");
		return ret;
	}
	
	return 0;
}

static const struct of_device_id trng_dt_ids[] = {
	{ .compatible = "starfive,trng" },
	{ }
};
MODULE_DEVICE_TABLE(of, trng_dt_ids);


static struct platform_driver trng_driver = {
	.probe		= trng_probe,
	.driver		= {
		.name		= "trng",
		.of_match_table	= of_match_ptr(trng_dt_ids),
	},
};

module_platform_driver(trng_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Jenny Zhang <jenny.zhang@starfivetech.com>");
MODULE_DESCRIPTION("Starfive true random number generator driver");
