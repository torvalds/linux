#ifndef S390_CIO_IOASM_H
#define S390_CIO_IOASM_H

#include <asm/chpid.h>
#include <asm/schid.h>
#include <asm/crw.h>
#include "orb.h"
#include "cio.h"
#include "trace.h"

/*
 * Some S390 specific IO instructions
 */

int stsch(struct subchannel_id schid, struct schib *addr);
int msch(struct subchannel_id schid, struct schib *addr);
int tsch(struct subchannel_id schid, struct irb *addr);
int ssch(struct subchannel_id schid, union orb *addr);
int csch(struct subchannel_id schid);
int tpi(struct tpi_info *addr);
int chsc(void *chsc_area);
int rchp(struct chp_id chpid);
int rsch(struct subchannel_id schid);
int hsch(struct subchannel_id schid);
int xsch(struct subchannel_id schid);
int stcrw(struct crw *crw);

#endif
