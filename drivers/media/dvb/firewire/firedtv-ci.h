#ifndef _FIREDTV_CI_H
#define _FIREDTV_CI_H

struct firedtv;

int fdtv_ca_register(struct firedtv *fdtv);
void fdtv_ca_release(struct firedtv *fdtv);

#endif /* _FIREDTV_CI_H */
