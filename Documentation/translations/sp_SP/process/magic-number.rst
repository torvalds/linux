.. include:: ../disclaimer-sp.rst

:Original: :ref:`Documentation/process/magic-number.rst <magicnumbers>`
:Translator: Carlos Bilbao <carlos.bilbao@amd.com>

.. _sp_magicnumbers:

Números mágicos de Linux
========================

Este archivo es un registro de los números mágicos que están en uso. Cuando
usted incluya un número mágico a una estructura, también debe agregarlo a
este documento, ya que es mejor si los números mágicos utilizados por
varias estructuras son únicos.

Es una muy buena idea proteger las estructuras de datos del kernel con
números mágicos. Esto le permite verificar en tiempo de ejecución si (a)
una estructura ha sido manipulada, o (b) ha pasado la estructura incorrecta
a una rutina. Esto último es especialmente útil --- particularmente cuando
pasa punteros a estructuras a través de un puntero void \*. El código tty,
por ejemplo, hace esto con frecuencia para pasar información específica del
driver y líneas de estructuras específicas de protocolo de un lado al
otro.

La forma de usar números mágicos es declararlos al principio de la
estructura, así::

	struct tty_ldisc {
		int	magic;
		...
	};

Por favor, siga este método cuando agregue futuras mejoras al kernel! Me ha
ahorrado innumerables horas de depuración, especialmente en los casos
complicados donde una matriz ha sido invadida y las estructuras que siguen
a la matriz se han sobrescrito. Usando este método, estos casos se detectan
de forma rápida y segura.

Changelog::

					Theodore Ts'o
					31 Mar 94

  La tabla mágica ha sido actualizada para Linux 2.1.55.

					Michael Chastain
					<mailto:mec@shout.net>
					22 Sep 1997

  Ahora debería estar actualizada con Linux 2.1.112. Porque
  estamos en fase de "feature freeze", es muy poco probable que
  algo cambiará antes de 2.2.x. Las entradas son
  ordenados por campo numérico.

					Krzysztof G. Baranowski
					<mailto: kgb@knm.org.pl>
					29 Jul 1998

  Se actualizó la tabla mágica a Linux 2.5.45. Justo sobre el feature
  freeze, pero es posible que algunos nuevos números mágicos se cuelen en
  el kernel antes de 2.6.x todavía.

					Petr Baudis
					<pasky@ucw.cz>
					03 Nov 2002

  La tabla mágica ha sido actualizada para Linux 2.5.74.

					Fabian Frederick
					<ffrederick@users.sourceforge.net>
					09 Jul 2003

===================== ================ ======================== ==========================================
Magic Name            Number           Structure                File
===================== ================ ======================== ==========================================
PG_MAGIC              'P'              pg_{read,write}_hdr      ``include/linux/pg.h``
APM_BIOS_MAGIC        0x4101           apm_user                 ``arch/x86/kernel/apm_32.c``
FASYNC_MAGIC          0x4601           fasync_struct            ``include/linux/fs.h``
SLIP_MAGIC            0x5302           slip                     ``drivers/net/slip.h``
MGSLPC_MAGIC          0x5402           mgslpc_info              ``drivers/char/pcmcia/synclink_cs.c``
BAYCOM_MAGIC          0x19730510       baycom_state             ``drivers/net/baycom_epp.c``
HDLCDRV_MAGIC         0x5ac6e778       hdlcdrv_state            ``include/linux/hdlcdrv.h``
KV_MAGIC              0x5f4b565f       kernel_vars_s            ``arch/mips/include/asm/sn/klkernvars.h``
CODA_MAGIC            0xC0DAC0DA       coda_file_info           ``fs/coda/coda_fs_i.h``
YAM_MAGIC             0xF10A7654       yam_port                 ``drivers/net/hamradio/yam.c``
CCB_MAGIC             0xf2691ad2       ccb                      ``drivers/scsi/ncr53c8xx.c``
QUEUE_MAGIC_FREE      0xf7e1c9a3       queue_entry              ``drivers/scsi/arm/queue.c``
QUEUE_MAGIC_USED      0xf7e1cc33       queue_entry              ``drivers/scsi/arm/queue.c``
NMI_MAGIC             0x48414d4d455201 nmi_s                    ``arch/mips/include/asm/sn/nmi.h``
===================== ================ ======================== ==========================================
