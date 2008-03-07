#ifndef __FIRESAT_LIRC_H
#define __FIRESAT_LIRC_H

extern int firesat_register_rc(void);
extern int firesat_unregister_rc(void);
extern int firesat_got_remotecontrolcode(u16 code);

#endif

