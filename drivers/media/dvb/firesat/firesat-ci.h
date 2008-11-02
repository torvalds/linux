#ifndef _FIREDTV_CI_H
#define _FIREDTV_CI_H

struct firesat;

int firesat_ca_register(struct firesat *firesat);
void firesat_ca_release(struct firesat *firesat);

#endif /* _FIREDTV_CI_H */
