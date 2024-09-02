#include "fbnic.h"

u64 fbnic_stat_rd64(struct fbnic_dev *fbd, u32 reg, u32 offset)
{
	u32 prev_upper, upper, lower, diff;

	prev_upper = rd32(fbd, reg + offset);
	lower = rd32(fbd, reg);
	upper = rd32(fbd, reg + offset);

	diff = upper - prev_upper;
	if (!diff)
		return ((u64)upper << 32) | lower;

	if (diff > 1)
		dev_warn_once(fbd->dev,
			      "Stats inconsistent, upper 32b of %#010x updating too quickly\n",
			      reg * 4);

	/* Return only the upper bits as we cannot guarantee
	 * the accuracy of the lower bits. We will add them in
	 * when the counter slows down enough that we can get
	 * a snapshot with both upper values being the same
	 * between reads.
	 */
	return ((u64)upper << 32);
}
