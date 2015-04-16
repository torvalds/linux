#include <linux/slab.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/reset-controller.h>

#include "reset.h"

#define rcdev_to_unit(rcdev) container_of(rcdev, struct mmp_clk_reset_unit, rcdev)

static int mmp_of_reset_xlate(struct reset_controller_dev *rcdev,
			  const struct of_phandle_args *reset_spec)
{
	struct mmp_clk_reset_unit *unit = rcdev_to_unit(rcdev);
	struct mmp_clk_reset_cell *cell;
	int i;

	if (WARN_ON(reset_spec->args_count != rcdev->of_reset_n_cells))
		return -EINVAL;

	for (i = 0; i < rcdev->nr_resets; i++) {
		cell = &unit->cells[i];
		if (cell->clk_id == reset_spec->args[0])
			break;
	}

	if (i == rcdev->nr_resets)
		return -EINVAL;

	return i;
}

static int mmp_clk_reset_assert(struct reset_controller_dev *rcdev,
				unsigned long id)
{
	struct mmp_clk_reset_unit *unit = rcdev_to_unit(rcdev);
	struct mmp_clk_reset_cell *cell;
	unsigned long flags = 0;
	u32 val;

	cell = &unit->cells[id];
	if (cell->lock)
		spin_lock_irqsave(cell->lock, flags);

	val = readl(cell->reg);
	val |= cell->bits;
	writel(val, cell->reg);

	if (cell->lock)
		spin_unlock_irqrestore(cell->lock, flags);

	return 0;
}

static int mmp_clk_reset_deassert(struct reset_controller_dev *rcdev,
				unsigned long id)
{
	struct mmp_clk_reset_unit *unit = rcdev_to_unit(rcdev);
	struct mmp_clk_reset_cell *cell;
	unsigned long flags = 0;
	u32 val;

	cell = &unit->cells[id];
	if (cell->lock)
		spin_lock_irqsave(cell->lock, flags);

	val = readl(cell->reg);
	val &= ~cell->bits;
	writel(val, cell->reg);

	if (cell->lock)
		spin_unlock_irqrestore(cell->lock, flags);

	return 0;
}

static struct reset_control_ops mmp_clk_reset_ops = {
	.assert		= mmp_clk_reset_assert,
	.deassert	= mmp_clk_reset_deassert,
};

void mmp_clk_reset_register(struct device_node *np,
			struct mmp_clk_reset_cell *cells, int nr_resets)
{
	struct mmp_clk_reset_unit *unit;

	unit = kzalloc(sizeof(*unit), GFP_KERNEL);
	if (!unit)
		return;

	unit->cells = cells;
	unit->rcdev.of_reset_n_cells = 1;
	unit->rcdev.nr_resets = nr_resets;
	unit->rcdev.ops = &mmp_clk_reset_ops;
	unit->rcdev.of_node = np;
	unit->rcdev.of_xlate = mmp_of_reset_xlate;

	reset_controller_register(&unit->rcdev);
}
