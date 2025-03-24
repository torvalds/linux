.. _magicnumbers:

Linux magic numbers
===================

This file is a registry of magic numbers which are in use.  When you
add a magic number to a structure, you should also add it to this
file, since it is best if the magic numbers used by various structures
are unique.

It is a **very** good idea to protect kernel data structures with magic
numbers.  This allows you to check at run time whether (a) a structure
has been clobbered, or (b) you've passed the wrong structure to a
routine.  This last is especially useful --- particularly when you are
passing pointers to structures via a void * pointer.  The tty code,
for example, does this frequently to pass driver-specific and line
discipline-specific structures back and forth.

The way to use magic numbers is to declare them at the beginning of
the structure, like so::

	struct tty_ldisc {
		int	magic;
		...
	};

Please follow this discipline when you are adding future enhancements
to the kernel!  It has saved me countless hours of debugging,
especially in the screwy cases where an array has been overrun and
structures following the array have been overwritten.  Using this
discipline, these cases get detected quickly and safely.

Changelog::

					Theodore Ts'o
					31 Mar 94

  The magic table is current to Linux 2.1.55.

					Michael Chastain
					<mailto:mec@shout.net>
					22 Sep 1997

  Now it should be up to date with Linux 2.1.112. Because
  we are in feature freeze time it is very unlikely that
  something will change before 2.2.x. The entries are
  sorted by number field.

					Krzysztof G. Baranowski
					<mailto: kgb@knm.org.pl>
					29 Jul 1998

  Updated the magic table to Linux 2.5.45. Right over the feature freeze,
  but it is possible that some new magic numbers will sneak into the
  kernel before 2.6.x yet.

					Petr Baudis
					<pasky@ucw.cz>
					03 Nov 2002

  Updated the magic table to Linux 2.5.74.

					Fabian Frederick
					<ffrederick@users.sourceforge.net>
					09 Jul 2003


===================== ================ ======================== ==========================================
Magic Name            Number           Structure                File
===================== ================ ======================== ==========================================
PG_MAGIC              'P'              pg_{read,write}_hdr      ``include/uapi/linux/pg.h``
APM_BIOS_MAGIC        0x4101           apm_user                 ``arch/x86/kernel/apm_32.c``
FASYNC_MAGIC          0x4601           fasync_struct            ``include/linux/fs.h``
SLIP_MAGIC            0x5302           slip                     ``drivers/net/slip/slip.h``
BAYCOM_MAGIC          19730510         baycom_state             ``drivers/net/hamradio/baycom_epp.c``
HDLCDRV_MAGIC         0x5ac6e778       hdlcdrv_state            ``include/linux/hdlcdrv.h``
KV_MAGIC              0x5f4b565f       kernel_vars_s            ``arch/mips/include/asm/sn/klkernvars.h``
CODA_MAGIC            0xC0DAC0DA       coda_file_info           ``fs/coda/coda_fs_i.h``
YAM_MAGIC             0xF10A7654       yam_port                 ``drivers/net/hamradio/yam.c``
CCB_MAGIC             0xf2691ad2       ccb                      ``drivers/scsi/ncr53c8xx.c``
QUEUE_MAGIC_FREE      0xf7e1c9a3       queue_entry              ``drivers/scsi/arm/queue.c``
QUEUE_MAGIC_USED      0xf7e1cc33       queue_entry              ``drivers/scsi/arm/queue.c``
NMI_MAGIC             0x48414d4d455201 nmi_s                    ``arch/mips/include/asm/sn/nmi.h``
===================== ================ ======================== ==========================================
