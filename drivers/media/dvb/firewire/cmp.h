#ifndef _CMP_H
#define _CMP_H

struct firedtv;

int cmp_establish_pp_connection(struct firedtv *fdtv, int plug, int channel);
void cmp_break_pp_connection(struct firedtv *fdtv, int plug, int channel);

#endif /* _CMP_H */
