#ifndef __FIRESAT__CMP_H_
#define __FIRESAT__CMP_H_

#include "firesat.h"

extern int try_CMPEstablishPPconnection(struct firesat *firesat, int output_plug, int iso_channel);
extern int try_CMPBreakPPconnection(struct firesat *firesat, int output_plug,int iso_channel);

#endif
