#ifndef _CMP_H
#define _CMP_H

struct firesat;

int cmp_establish_pp_connection(struct firesat *firesat, int plug, int channel);
void cmp_break_pp_connection(struct firesat *firesat, int plug, int channel);

#endif /* _CMP_H */
