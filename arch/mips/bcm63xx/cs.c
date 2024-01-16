/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2008 Maxime Bizon <mbizon@freebox.fr>
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/spinlock.h>
#include <linux/log2.h>
#include <bcm63xx_cpu.h>
#include <bcm63xx_io.h>
#include <bcm63xx_regs.h>
#include <bcm63xx_cs.h>

static DEFINE_SPINLOCK(bcm63xx_cs_lock);

/*
 * check if given chip select exists
 */
static int is_valid_cs(unsigned int cs)
{
	if (cs > 6)
		return 0;
	return 1;
}

/*
 * Configure chipselect base address and size (bytes).
 * Size must be a power of two between 8k and 256M.
 */
int bcm63xx_set_cs_base(unsigned int cs, u32 base, unsigned int size)
{
	unsigned long flags;
	u32 val;

	if (!is_valid_cs(cs))
		return -EINVAL;

	/* sanity check on size */
	if (size != roundup_pow_of_two(size))
		return -EINVAL;

	if (size < 8 * 1024 || size > 256 * 1024 * 1024)
		return -EINVAL;

	val = (base & MPI_CSBASE_BASE_MASK);
	/* 8k => 0 - 256M => 15 */
	val |= (ilog2(size) - ilog2(8 * 1024)) << MPI_CSBASE_SIZE_SHIFT;

	spin_lock_irqsave(&bcm63xx_cs_lock, flags);
	bcm_mpi_writel(val, MPI_CSBASE_REG(cs));
	spin_unlock_irqrestore(&bcm63xx_cs_lock, flags);

	return 0;
}

EXPORT_SYMBOL(bcm63xx_set_cs_base);

/*
 * configure chipselect timing (ns)
 */
int bcm63xx_set_cs_timing(unsigned int cs, unsigned int wait,
			   unsigned int setup, unsigned int hold)
{
	unsigned long flags;
	u32 val;

	if (!is_valid_cs(cs))
		return -EINVAL;

	spin_lock_irqsave(&bcm63xx_cs_lock, flags);
	val = bcm_mpi_readl(MPI_CSCTL_REG(cs));
	val &= ~(MPI_CSCTL_WAIT_MASK);
	val &= ~(MPI_CSCTL_SETUP_MASK);
	val &= ~(MPI_CSCTL_HOLD_MASK);
	val |= wait << MPI_CSCTL_WAIT_SHIFT;
	val |= setup << MPI_CSCTL_SETUP_SHIFT;
	val |= hold << MPI_CSCTL_HOLD_SHIFT;
	bcm_mpi_writel(val, MPI_CSCTL_REG(cs));
	spin_unlock_irqrestore(&bcm63xx_cs_lock, flags);

	return 0;
}

EXPORT_SYMBOL(bcm63xx_set_cs_timing);

/*
 * configure other chipselect parameter (data bus size, ...)
 */
int bcm63xx_set_cs_param(unsigned int cs, u32 params)
{
	unsigned long flags;
	u32 val;

	if (!is_valid_cs(cs))
		return -EINVAL;

	/* none of this fields apply to pcmcia */
	if (cs == MPI_CS_PCMCIA_COMMON ||
	    cs == MPI_CS_PCMCIA_ATTR ||
	    cs == MPI_CS_PCMCIA_IO)
		return -EINVAL;

	spin_lock_irqsave(&bcm63xx_cs_lock, flags);
	val = bcm_mpi_readl(MPI_CSCTL_REG(cs));
	val &= ~(MPI_CSCTL_DATA16_MASK);
	val &= ~(MPI_CSCTL_SYNCMODE_MASK);
	val &= ~(MPI_CSCTL_TSIZE_MASK);
	val &= ~(MPI_CSCTL_ENDIANSWAP_MASK);
	val |= params;
	bcm_mpi_writel(val, MPI_CSCTL_REG(cs));
	spin_unlock_irqrestore(&bcm63xx_cs_lock, flags);

	return 0;
}

EXPORT_SYMBOL(bcm63xx_set_cs_param);

/*
 * set cs status (enable/disable)
 */
int bcm63xx_set_cs_status(unsigned int cs, int enable)
{
	unsigned long flags;
	u32 val;

	if (!is_valid_cs(cs))
		return -EINVAL;

	spin_lock_irqsave(&bcm63xx_cs_lock, flags);
	val = bcm_mpi_readl(MPI_CSCTL_REG(cs));
	if (enable)
		val |= MPI_CSCTL_ENABLE_MASK;
	else
		val &= ~MPI_CSCTL_ENABLE_MASK;
	bcm_mpi_writel(val, MPI_CSCTL_REG(cs));
	spin_unlock_irqrestore(&bcm63xx_cs_lock, flags);
	return 0;
}

EXPORT_SYMBOL(bcm63xx_set_cs_status);
