#ifndef SWPHY_H
#define SWPHY_H

struct fixed_phy_status;

int swphy_update_regs(u16 *regs, const struct fixed_phy_status *state);

#endif
