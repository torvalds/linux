#ifndef __ARCH_CRIS_IOCTLS_H__
#define __ARCH_CRIS_IOCTLS_H__

#define TIOCSERGSTRUCT	0x5458 /* For debugging only */
#define TIOCSERSETRS485	0x5461  /* enable rs-485 (deprecated) */
#define TIOCSERWRRS485	0x5462  /* write rs-485 */
#define TIOCSRS485	0x5463  /* enable rs-485 */

#include <asm-generic/ioctls.h>

#endif
