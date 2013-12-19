#include <linux/clk.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include "clk.h"

DEFINE_SPINLOCK(imx_ccm_lock);

static struct clk * __init imx_obtain_fixed_clock_from_dt(const char *name)
{
	struct of_phandle_args phandle;
	struct clk *clk = ERR_PTR(-ENODEV);
	char *path;

	path = kasprintf(GFP_KERNEL, "/clocks/%s", name);
	if (!path)
		return ERR_PTR(-ENOMEM);

	phandle.np = of_find_node_by_path(path);
	kfree(path);

	if (phandle.np) {
		clk = of_clk_get_from_provider(&phandle);
		of_node_put(phandle.np);
	}
	return clk;
}

struct clk * __init imx_obtain_fixed_clock(
			const char *name, unsigned long rate)
{
	struct clk *clk;

	clk = imx_obtain_fixed_clock_from_dt(name);
	if (IS_ERR(clk))
		clk = imx_clk_fixed(name, rate);
	return clk;
}

/*
 * This fixups the register CCM_CSCMR1 write value.
 * The write/read/divider values of the aclk_podf field
 * of that register have the relationship described by
 * the following table:
 *
 * write value       read value        divider
 * 3b'000            3b'110            7
 * 3b'001            3b'111            8
 * 3b'010            3b'100            5
 * 3b'011            3b'101            6
 * 3b'100            3b'010            3
 * 3b'101            3b'011            4
 * 3b'110            3b'000            1
 * 3b'111            3b'001            2(default)
 *
 * That's why we do the xor operation below.
 */
#define CSCMR1_FIXUP	0x00600000

void imx_cscmr1_fixup(u32 *val)
{
	*val ^= CSCMR1_FIXUP;
	return;
}
