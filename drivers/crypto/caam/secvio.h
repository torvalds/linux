
/*
 * CAAM Security Violation Handler
 * Copyright (C) 2015 Freescale Semiconductor, Inc., All Rights Reserved
 */

#ifndef SECVIO_H
#define SECVIO_H

#include "snvsregs.h"


/*
 * Defines the published interfaces to install/remove application-specified
 * handlers for catching violations
 */

#define MAX_SECVIO_SOURCES 6

/* these are the untranslated causes */
enum secvio_cause {
	SECVIO_CAUSE_SOURCE_0,
	SECVIO_CAUSE_SOURCE_1,
	SECVIO_CAUSE_SOURCE_2,
	SECVIO_CAUSE_SOURCE_3,
	SECVIO_CAUSE_SOURCE_4,
	SECVIO_CAUSE_SOURCE_5
};

/* These are common "recommended" cause definitions for most devices */
#define SECVIO_CAUSE_CAAM_VIOLATION SECVIO_CAUSE_SOURCE_0
#define SECVIO_CAUSE JTAG_ALARM SECVIO_CAUSE_SOURCE_1
#define SECVIO_CAUSE_WATCHDOG SECVIO_CAUSE_SOURCE_2
#define SECVIO_CAUSE_EXTERNAL_BOOT SECVIO_CAUSE_SOURCE_4
#define SECVIO_CAUSE_TAMPER_DETECT SECVIO_CAUSE_SOURCE_5

int caam_secvio_install_handler(struct device *dev, enum secvio_cause cause,
				void (*handler)(struct device *dev, u32 cause,
						void *ext),
				u8 *cause_description, void *ext);
int caam_secvio_remove_handler(struct device *dev, enum  secvio_cause cause);

/*
 * Private data definitions for the secvio "driver"
 */

struct secvio_int_src {
	const u8 *intname;	/* Points to a descriptive name for source */
	void *ext;		/* Extended data to pass to the handler */
	void (*handler)(struct device *dev, u32 cause, void *ext);
};

struct caam_drv_private_secvio {
	struct device *parentdev;	/* points back to the controller */
	spinlock_t svlock ____cacheline_aligned;
	struct tasklet_struct irqtask[NR_CPUS];
	struct snvs_full __iomem *svregs;	/* both HP and LP domains */

	/* Registered handlers for each violation */
	struct secvio_int_src intsrc[MAX_SECVIO_SOURCES];

};

#endif /* SECVIO_H */
