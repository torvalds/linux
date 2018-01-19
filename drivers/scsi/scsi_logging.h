/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SCSI_LOGGING_H
#define _SCSI_LOGGING_H


/*
 * This defines the scsi logging feature.  It is a means by which the user can
 * select how much information they get about various goings on, and it can be
 * really useful for fault tracing.  The logging word is divided into 10 3-bit
 * bitfields, each of which describes a loglevel.  The division of things is
 * somewhat arbitrary, and the division of the word could be changed if it
 * were really needed for any reason.  The numbers below are the only place
 * where these are specified.  For a first go-around, 3 bits is more than
 * enough, since this gives 8 levels of logging (really 7, since 0 is always
 * off).  Cutting to 2 bits might be wise at some point.
 */

#define SCSI_LOG_ERROR_SHIFT              0
#define SCSI_LOG_TIMEOUT_SHIFT            3
#define SCSI_LOG_SCAN_SHIFT               6
#define SCSI_LOG_MLQUEUE_SHIFT            9
#define SCSI_LOG_MLCOMPLETE_SHIFT         12
#define SCSI_LOG_LLQUEUE_SHIFT            15
#define SCSI_LOG_LLCOMPLETE_SHIFT         18
#define SCSI_LOG_HLQUEUE_SHIFT            21
#define SCSI_LOG_HLCOMPLETE_SHIFT         24
#define SCSI_LOG_IOCTL_SHIFT              27

#define SCSI_LOG_ERROR_BITS               3
#define SCSI_LOG_TIMEOUT_BITS             3
#define SCSI_LOG_SCAN_BITS                3
#define SCSI_LOG_MLQUEUE_BITS             3
#define SCSI_LOG_MLCOMPLETE_BITS          3
#define SCSI_LOG_LLQUEUE_BITS             3
#define SCSI_LOG_LLCOMPLETE_BITS          3
#define SCSI_LOG_HLQUEUE_BITS             3
#define SCSI_LOG_HLCOMPLETE_BITS          3
#define SCSI_LOG_IOCTL_BITS               3

extern unsigned int scsi_logging_level;

#ifdef CONFIG_SCSI_LOGGING

#define SCSI_LOG_LEVEL(SHIFT, BITS)				\
        ((scsi_logging_level >> (SHIFT)) & ((1 << (BITS)) - 1))

#define SCSI_CHECK_LOGGING(SHIFT, BITS, LEVEL, CMD)		\
do {								\
        if (unlikely((SCSI_LOG_LEVEL(SHIFT, BITS)) > (LEVEL)))	\
		do {						\
			CMD;					\
		} while (0);					\
} while (0)
#else
#define SCSI_LOG_LEVEL(SHIFT, BITS) 0
#define SCSI_CHECK_LOGGING(SHIFT, BITS, LEVEL, CMD)
#endif /* CONFIG_SCSI_LOGGING */

/*
 * These are the macros that are actually used throughout the code to
 * log events.  If logging isn't enabled, they are no-ops and will be
 * completely absent from the user's code.
 */
#define SCSI_LOG_ERROR_RECOVERY(LEVEL,CMD)  \
        SCSI_CHECK_LOGGING(SCSI_LOG_ERROR_SHIFT, SCSI_LOG_ERROR_BITS, LEVEL,CMD);
#define SCSI_LOG_TIMEOUT(LEVEL,CMD)  \
        SCSI_CHECK_LOGGING(SCSI_LOG_TIMEOUT_SHIFT, SCSI_LOG_TIMEOUT_BITS, LEVEL,CMD);
#define SCSI_LOG_SCAN_BUS(LEVEL,CMD)  \
        SCSI_CHECK_LOGGING(SCSI_LOG_SCAN_SHIFT, SCSI_LOG_SCAN_BITS, LEVEL,CMD);
#define SCSI_LOG_MLQUEUE(LEVEL,CMD)  \
        SCSI_CHECK_LOGGING(SCSI_LOG_MLQUEUE_SHIFT, SCSI_LOG_MLQUEUE_BITS, LEVEL,CMD);
#define SCSI_LOG_MLCOMPLETE(LEVEL,CMD)  \
        SCSI_CHECK_LOGGING(SCSI_LOG_MLCOMPLETE_SHIFT, SCSI_LOG_MLCOMPLETE_BITS, LEVEL,CMD);
#define SCSI_LOG_LLQUEUE(LEVEL,CMD)  \
        SCSI_CHECK_LOGGING(SCSI_LOG_LLQUEUE_SHIFT, SCSI_LOG_LLQUEUE_BITS, LEVEL,CMD);
#define SCSI_LOG_LLCOMPLETE(LEVEL,CMD)  \
        SCSI_CHECK_LOGGING(SCSI_LOG_LLCOMPLETE_SHIFT, SCSI_LOG_LLCOMPLETE_BITS, LEVEL,CMD);
#define SCSI_LOG_HLQUEUE(LEVEL,CMD)  \
        SCSI_CHECK_LOGGING(SCSI_LOG_HLQUEUE_SHIFT, SCSI_LOG_HLQUEUE_BITS, LEVEL,CMD);
#define SCSI_LOG_HLCOMPLETE(LEVEL,CMD)  \
        SCSI_CHECK_LOGGING(SCSI_LOG_HLCOMPLETE_SHIFT, SCSI_LOG_HLCOMPLETE_BITS, LEVEL,CMD);
#define SCSI_LOG_IOCTL(LEVEL,CMD)  \
        SCSI_CHECK_LOGGING(SCSI_LOG_IOCTL_SHIFT, SCSI_LOG_IOCTL_BITS, LEVEL,CMD);

#endif /* _SCSI_LOGGING_H */
