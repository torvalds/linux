#ifndef AMIGA7XX_H

#include <linux/types.h>

int amiga7xx_detect(struct scsi_host_template *);
const char *NCR53c7x0_info(void);
int NCR53c7xx_queue_command(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));
int NCR53c7xx_abort(Scsi_Cmnd *);
int NCR53c7x0_release (struct Scsi_Host *);
int NCR53c7xx_reset(Scsi_Cmnd *, unsigned int);
void NCR53c7x0_intr(int irq, void *dev_id, struct pt_regs * regs);

#ifndef CMD_PER_LUN
#define CMD_PER_LUN 3
#endif

#ifndef CAN_QUEUE
#define CAN_QUEUE 24
#endif

#include <scsi/scsicam.h>

#endif /* AMIGA7XX_H */
