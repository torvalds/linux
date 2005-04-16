/*
 *	There is probably a nicer way to do this but this one makes
 *	pretty obvious what is happening. We rebuild the same file with
 *	different options for mmio versus pio.
 */

#define SCSI_G_NCR5380_MEM

#include "g_NCR5380.c"

