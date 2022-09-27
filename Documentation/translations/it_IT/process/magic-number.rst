.. include:: ../disclaimer-ita.rst

:Original: :ref:`Documentation/process/magic-number.rst <magicnumbers>`
:Translator: Federico Vaga <federico.vaga@vaga.pv.it>

.. _it_magicnumbers:

I numeri magici di Linux
========================

Questo documento è un registro dei numeri magici in uso.  Quando
aggiungete un numero magico ad una struttura, dovreste aggiungerlo anche
a questo documento; la cosa migliore è che tutti i numeri magici usati
dalle varie strutture siano unici.

È **davvero** un'ottima idea proteggere le strutture dati del kernel con
dei numeri magici.  Questo vi permette in fase d'esecuzione di (a) verificare
se una struttura è stata malmenata, o (b) avete passato a una procedura la
struttura errata.  Quest'ultimo è molto utile - particolarmente quando si passa
una struttura dati tramite un puntatore void \*.  Il codice tty, per esempio,
effettua questa operazione con regolarità passando avanti e indietro le
strutture specifiche per driver e discipline.

Per utilizzare un numero magico, dovete dichiararlo all'inizio della struttura
dati, come di seguito::

	struct tty_ldisc {
		int	magic;
		...
	};

Per favore, seguite questa direttiva quando aggiungerete migliorie al kernel!
Mi ha risparmiato un numero illimitato di ore di debug, specialmente nei casi
più ostici dove si è andati oltre la dimensione di un vettore e la struttura
dati che lo seguiva in memoria è stata sovrascritta.  Seguendo questa
direttiva, questi casi vengono identificati velocemente e in sicurezza.

Registro dei cambiamenti::

					Theodore Ts'o
					31 Mar 94

  La tabella magica è aggiornata a Linux 2.1.55.

					Michael Chastain
					<mailto:mec@shout.net>
					22 Sep 1997

  Ora dovrebbe essere aggiornata a Linux 2.1.112. Dato che
  siamo in un momento di congelamento delle funzionalità
  (*feature freeze*) è improbabile che qualcosa cambi prima
  della versione 2.2.x.  Le righe sono ordinate secondo il
  campo numero.

					Krzysztof G. Baranowski
					<mailto: kgb@knm.org.pl>
					29 Jul 1998

  Aggiornamento della tabella a Linux 2.5.45. Giusti nel congelamento
  delle funzionalità ma è comunque possibile che qualche nuovo
  numero magico s'intrufoli prima del kernel 2.6.x.

					Petr Baudis
					<pasky@ucw.cz>
					03 Nov 2002

  Aggiornamento della tabella magica a Linux 2.5.74.

					Fabian Frederick
					<ffrederick@users.sourceforge.net>
					09 Jul 2003


===================== ================ ======================== ==========================================
Nome magico           Numero           Struttura                File
===================== ================ ======================== ==========================================
PG_MAGIC              'P'              pg_{read,write}_hdr      ``include/linux/pg.h``
HDLC_MAGIC            0x239e           n_hdlc                   ``drivers/char/n_hdlc.c``
APM_BIOS_MAGIC        0x4101           apm_user                 ``arch/x86/kernel/apm_32.c``
FASYNC_MAGIC          0x4601           fasync_struct            ``include/linux/fs.h``
SLIP_MAGIC            0x5302           slip                     ``drivers/net/slip.h``
TTY_MAGIC             0x5401           tty_struct               ``include/linux/tty.h``
MGSL_MAGIC            0x5401           mgsl_info                ``drivers/char/synclink.c``
TTY_DRIVER_MAGIC      0x5402           tty_driver               ``include/linux/tty_driver.h``
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
