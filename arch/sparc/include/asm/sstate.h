#ifndef _SPARC64_SSTATE_H
#define _SPARC64_SSTATE_H

extern void sstate_booting(void);
extern void sstate_running(void);
extern void sstate_halt(void);
extern void sstate_poweroff(void);
extern void sstate_panic(void);
extern void sstate_reboot(void);

extern void sun4v_sstate_init(void);

#endif /* _SPARC64_SSTATE_H */
