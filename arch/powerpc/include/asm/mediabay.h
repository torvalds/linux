/*
 * mediabay.h: definitions for using the media bay
 * on PowerBook 3400 and similar computers.
 *
 * Copyright (C) 1997 Paul Mackerras.
 */
#ifndef _PPC_MEDIABAY_H
#define _PPC_MEDIABAY_H

#ifdef __KERNEL__

#define MB_FD		0	/* media bay contains floppy drive (automatic eject ?) */
#define MB_FD1		1	/* media bay contains floppy drive (manual eject ?) */
#define MB_SOUND	2	/* sound device ? */
#define MB_CD		3	/* media bay contains ATA drive such as CD or ZIP */
#define MB_PCI		5	/* media bay contains a PCI device */
#define MB_POWER	6	/* media bay contains a Power device (???) */
#define MB_NO		7	/* media bay contains nothing */

struct macio_dev;

#ifdef CONFIG_PMAC_MEDIABAY

/* Check the content type of the bay, returns MB_NO if the bay is still
 * transitionning
 */
extern int check_media_bay(struct macio_dev *bay);

/* The ATA driver uses the calls below to temporarily hold on the
 * media bay callbacks while initializing the interface
 */
extern void lock_media_bay(struct macio_dev *bay);
extern void unlock_media_bay(struct macio_dev *bay);

#else

static inline int check_media_bay(struct macio_dev *bay)
{
	return MB_NO;
}

static inline void lock_media_bay(struct macio_dev *bay) { }
static inline void unlock_media_bay(struct macio_dev *bay) { }

#endif

#endif /* __KERNEL__ */
#endif /* _PPC_MEDIABAY_H */
