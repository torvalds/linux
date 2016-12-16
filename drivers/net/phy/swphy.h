#ifndef SWPHY_H
#define SWPHY_H

struct fixed_phy_status;

int swphy_validate_state(const struct fixed_phy_status *state);
int swphy_read_reg(int reg, const struct fixed_phy_status *state);

#endif
