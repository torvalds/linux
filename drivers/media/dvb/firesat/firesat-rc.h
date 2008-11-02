#ifndef _FIREDTV_RC_H
#define _FIREDTV_RC_H

struct firesat;
struct device;

int firesat_register_rc(struct firesat *firesat, struct device *dev);
void firesat_unregister_rc(struct firesat *firesat);
void firesat_handle_rc(struct firesat *firesat, unsigned int code);

#endif /* _FIREDTV_RC_H */
