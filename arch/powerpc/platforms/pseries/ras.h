#ifndef _PSERIES_RAS_H
#define _PSERIES_RAS_H

struct pt_regs;

extern int pSeries_system_reset_exception(struct pt_regs *regs);
extern int pSeries_machine_check_exception(struct pt_regs *regs);

#endif /* _PSERIES_RAS_H */
