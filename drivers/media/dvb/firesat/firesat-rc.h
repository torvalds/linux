#ifndef _FIREDTV_RC_H
#define _FIREDTV_RC_H

int firesat_register_rc(void);
void firesat_unregister_rc(void);
void firesat_handle_rc(unsigned int code);

#endif /* _FIREDTV_RC_H */
