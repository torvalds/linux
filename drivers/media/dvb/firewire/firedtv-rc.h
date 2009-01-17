#ifndef _FIREDTV_RC_H
#define _FIREDTV_RC_H

struct firedtv;
struct device;

int fdtv_register_rc(struct firedtv *fdtv, struct device *dev);
void fdtv_unregister_rc(struct firedtv *fdtv);
void fdtv_handle_rc(struct firedtv *fdtv, unsigned int code);

#endif /* _FIREDTV_RC_H */
