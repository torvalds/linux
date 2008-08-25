#ifndef _CMP_H
#define _CMP_H

struct firesat;

int try_CMPEstablishPPconnection(struct firesat *firesat, int output_plug,
		int iso_channel);
int try_CMPBreakPPconnection(struct firesat *firesat, int output_plug,
		int iso_channel);

#endif /* _CMP_H */
