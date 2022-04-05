.. include:: ../disclaimer-ita.rst

:Original: :ref:`Documentation/process/changes.rst <changes>`
:Translator: Federico Vaga <federico.vaga@vaga.pv.it>

.. _it_changes:

Requisiti minimi per compilare il kernel
++++++++++++++++++++++++++++++++++++++++

Introduzione
============

Questo documento fornisce una lista dei software necessari per eseguire i
kernel 4.x.

Questo documento è basato sul file "Changes" del kernel 2.0.x e quindi le
persone che lo scrissero meritano credito (Jared Mauch, Axel Boldt,
Alessandro Sigala, e tanti altri nella rete).

Requisiti minimi correnti
*************************

Prima di pensare d'avere trovato un baco, aggiornate i seguenti programmi
**almeno** alla versione indicata!  Se non siete certi della versione che state
usando, il comando indicato dovrebbe dirvelo.

Questa lista presume che abbiate già un kernel Linux funzionante.  In aggiunta,
non tutti gli strumenti sono necessari ovunque; ovviamente, se non avete una
PC Card, per esempio, probabilmente non dovreste preoccuparvi di pcmciautils.

====================== =================  ========================================
        Programma       Versione minima       Comando per verificare la versione
====================== =================  ========================================
GNU C                  4.6                gcc --version
GNU make               3.81               make --version
binutils               2.21               ld -v
flex                   2.5.35             flex --version
bison                  2.0                bison --version
util-linux             2.10o              fdformat --version
kmod                   13                 depmod -V
e2fsprogs              1.41.4             e2fsck -V
jfsutils               1.1.3              fsck.jfs -V
reiserfsprogs          3.6.3              reiserfsck -V
xfsprogs               2.6.0              xfs_db -V
squashfs-tools         4.0                mksquashfs -version
btrfs-progs            0.18               btrfsck
pcmciautils            004                pccardctl -V
quota-tools            3.09               quota -V
PPP                    2.4.0              pppd --version
nfs-utils              1.0.5              showmount --version
procps                 3.2.0              ps --version
oprofile               0.9                oprofiled --version
udev                   081                udevd --version
grub                   0.93               grub --version || grub-install --version
mcelog                 0.6                mcelog --version
iptables               1.4.2              iptables -V
openssl & libcrypto    1.0.0              openssl version
bc                     1.06.95            bc --version
Sphinx\ [#f1]_         1.3                sphinx-build --version
====================== =================  ========================================

.. [#f1] Sphinx è necessario solo per produrre la documentazione del Kernel

Compilazione del kernel
***********************

GCC
---

La versione necessaria di gcc potrebbe variare a seconda del tipo di CPU nel
vostro calcolatore.

Make
----

Per compilare il kernel vi servirà GNU make 3.81 o successivo.

Binutils
--------

Per generare il kernel è necessario avere Binutils 2.21 o superiore.

pkg-config
----------

Il sistema di compilazione, dalla versione 4.18, richiede pkg-config per
verificare l'esistenza degli strumenti kconfig e per determinare le
impostazioni da usare in 'make {g,x}config'.  Precedentemente pkg-config
veniva usato ma non verificato o documentato.

Flex
----

Dalla versione 4.16, il sistema di compilazione, durante l'esecuzione, genera
un analizzatore lessicale.  Questo richiede flex 2.5.35 o successivo.

Bison
-----

Dalla versione 4.16, il sistema di compilazione, durante l'esecuzione, genera
un parsificatore.  Questo richiede bison 2.0 o successivo.

Perl
----

Per compilare il kernel vi servirà perl 5 e i seguenti moduli ``Getopt::Long``,
``Getopt::Std``, ``File::Basename``, e ``File::Find``.

BC
--

Vi servirà bc per compilare i kernel dal 3.10 in poi.

OpenSSL
-------

Il programma OpenSSL e la libreria crypto vengono usati per la firma dei moduli
e la gestione dei certificati; sono usati per la creazione della chiave e
la generazione della firma.

Se la firma dei moduli è abilitata, allora vi servirà openssl per compilare il
kernel 3.7 e successivi.  Vi serviranno anche i pacchetti di sviluppo di
openssl per compilare il kernel 4.3 o successivi.


Strumenti di sistema
********************

Modifiche architetturali
------------------------

DevFS è stato reso obsoleto da udev
(http://www.kernel.org/pub/linux/utils/kernel/hotplug/)

Il supporto per UID a 32-bit è ora disponibile.  Divertitevi!

La documentazione delle funzioni in Linux è una fase di transizione
verso una documentazione integrata nei sorgenti stessi usando dei commenti
formattati in modo speciale e posizionati vicino alle funzioni che descrivono.
Al fine di arricchire la documentazione, questi commenti possono essere
combinati con i file ReST presenti in Documentation/; questi potranno
poi essere convertiti in formato PostScript, HTML, LaTex, ePUB o PDF.
Per convertire i documenti da ReST al formato che volete, avete bisogno di
Sphinx.

Util-linux
----------

Le versioni più recenti di util-linux: forniscono il supporto a ``fdisk`` per
dischi di grandi dimensioni; supportano le nuove opzioni di mount; riconoscono
più tipi di partizioni; hanno un fdformat che funziona con i kernel 2.4;
e altre chicche.  Probabilmente vorrete aggiornarlo.

Ksymoops
--------

Se l'impensabile succede e il kernel va in oops, potrebbe servirvi lo strumento
ksymoops per decodificarlo, ma nella maggior parte dei casi non vi servirà.
Generalmente è preferibile compilare il kernel con l'opzione ``CONFIG_KALLSYMS``
cosicché venga prodotto un output più leggibile che può essere usato così com'è
(produce anche un output migliore di ksymoops).  Se per qualche motivo il
vostro kernel non è stato compilato con ``CONFIG_KALLSYMS`` e non avete modo di
ricompilarlo e riprodurre l'oops con quell'opzione abilitata, allora potete
usare ksymoops per decodificare l'oops.

Mkinitrd
--------

I cambiamenti della struttura in ``/lib/modules`` necessita l'aggiornamento di
mkinitrd.

E2fsprogs
---------

L'ultima versione di ``e2fsprogs`` corregge diversi bachi in fsck e debugfs.
Ovviamente, aggiornarlo è una buona idea.

JFSutils
--------

Il pacchetto ``jfsutils`` contiene programmi per il file-system JFS.
Sono disponibili i seguenti strumenti:

- ``fsck.jfs`` - avvia la ripetizione del log delle transizioni, e verifica e
  ripara una partizione formattata secondo JFS

- ``mkfs.jfs`` - crea una partizione formattata secondo JFS

- sono disponibili altri strumenti per il file-system.

Reiserfsprogs
-------------

Il pacchetto reiserfsprogs dovrebbe essere usato con reiserfs-3.6.x (Linux
kernel 2.4.x).  Questo è un pacchetto combinato che contiene versioni
funzionanti di ``mkreiserfs``, ``resize_reiserfs``, ``debugreiserfs`` e
``reiserfsck``.  Questi programmi funzionano sulle piattaforme i386 e alpha.

Xfsprogs
--------

L'ultima versione di ``xfsprogs`` contiene, fra i tanti, i programmi
``mkfs.xfs``, ``xfs_db`` e ``xfs_repair`` per il file-system XFS.
Dipendono dell'architettura e qualsiasi versione dalla 2.0.0 in poi
dovrebbe funzionare correttamente con la versione corrente del codice
XFS nel kernel (sono raccomandate le versioni 2.6.0 o successive per via
di importanti miglioramenti).

PCMCIAutils
-----------

PCMCIAutils sostituisce ``pcmica-cs``.  Serve ad impostare correttamente i
connettori PCMCIA all'avvio del sistema e a caricare i moduli necessari per
i dispositivi a 16-bit se il kernel è stato modularizzato e il sottosistema
hotplug è in uso.

Quota-tools
-----------

Il supporto per uid e gid a 32 bit richiedono l'uso della versione 2 del
formato quota.  La versione 3.07 e successive di quota-tools supportano
questo formato.  Usate la versione raccomandata nella lista qui sopra o una
successiva.

Micro codice per Intel IA32
---------------------------

Per poter aggiornare il micro codice per Intel IA32, è stato aggiunto un
apposito driver; il driver è accessibile come un normale dispositivo a
caratteri (misc).  Se non state usando udev probabilmente sarà necessario
eseguire i seguenti comandi come root prima di poterlo aggiornare::

  mkdir /dev/cpu
  mknod /dev/cpu/microcode c 10 184
  chmod 0644 /dev/cpu/microcode

Probabilmente, vorrete anche il programma microcode_ctl da usare con questo
dispositivo.

udev
----

``udev`` è un programma in spazio utente il cui scopo è quello di popolare
dinamicamente la cartella ``/dev`` coi dispositivi effettivamente presenti.
``udev`` sostituisce le funzionalità base di devfs, consentendo comunque
nomi persistenti per i dispositivi.

FUSE
----

Serve libfuse 2.4.0 o successiva.  Il requisito minimo assoluto è 2.3.0 ma
le opzioni di mount ``direct_io`` e ``kernel_cache`` non funzioneranno.


Rete
****

Cambiamenti generali
--------------------

Se per quanto riguarda la configurazione di rete avete esigenze di un certo
livello dovreste prendere in considerazione l'uso degli strumenti in ip-route2.

Filtro dei pacchetti / NAT
--------------------------

Il codice per filtraggio dei pacchetti e il NAT fanno uso degli stessi
strumenti come nelle versioni del kernel antecedenti la 2.4.x (iptables).
Include ancora moduli di compatibilità per 2.2.x ipchains e 2.0.x ipdwadm.

PPP
---

Il driver per PPP è stato ristrutturato per supportare collegamenti multipli e
per funzionare su diversi livelli.  Se usate PPP, aggiornate pppd almeno alla
versione 2.4.0.

Se non usate udev, dovete avere un file /dev/ppp che può essere creato da root
col seguente comando::

  mknod /dev/ppp c 108 0


NFS-utils
---------

Nei kernel più antichi (2.4 e precedenti), il server NFS doveva essere
informato sui clienti ai quali si voleva fornire accesso via NFS.  Questa
informazione veniva passata al kernel quando un cliente montava un file-system
mediante ``mountd``, oppure usando ``exportfs`` all'avvio del sistema.
exportfs prende le informazioni circa i clienti attivi da ``/var/lib/nfs/rmtab``.

Questo approccio è piuttosto delicato perché dipende dalla correttezza di
rmtab, che non è facile da garantire, in particolare quando si cerca di
implementare un *failover*.  Anche quando il sistema funziona bene, ``rmtab``
ha il problema di accumulare vecchie voci inutilizzate.

Sui kernel più recenti il kernel ha la possibilità di informare mountd quando
arriva una richiesta da una macchina sconosciuta, e mountd può dare al kernel
le informazioni corrette per l'esportazione.  Questo rimuove la dipendenza con
``rmtab`` e significa che il kernel deve essere al corrente solo dei clienti
attivi.

Per attivare questa funzionalità, dovete eseguire il seguente comando prima di
usare exportfs o mountd::

  mount -t nfsd nfsd /proc/fs/nfsd

Dove possibile, raccomandiamo di proteggere tutti i servizi NFS dall'accesso
via internet mediante un firewall.

mcelog
------

Quando ``CONFIG_x86_MCE`` è attivo, il programma mcelog processa e registra
gli eventi *machine check*.  Gli eventi *machine check* sono errori riportati
dalla CPU.  Incoraggiamo l'analisi di questi errori.


Documentazione del kernel
*************************

Sphinx
------

Per i dettaglio sui requisiti di Sphinx, fate riferimento a :ref:`it_sphinx_install`
in :ref:`Documentation/translations/it_IT/doc-guide/sphinx.rst <it_sphinxdoc>`

Ottenere software aggiornato
============================

Compilazione del kernel
***********************

gcc
---

- <ftp://ftp.gnu.org/gnu/gcc/>

Make
----

- <ftp://ftp.gnu.org/gnu/make/>

Binutils
--------

- <https://www.kernel.org/pub/linux/devel/binutils/>

Flex
----

- <https://github.com/westes/flex/releases>

Bison
-----

- <ftp://ftp.gnu.org/gnu/bison/>

OpenSSL
-------

- <https://www.openssl.org/>

Strumenti di sistema
********************

Util-linux
----------

- <https://www.kernel.org/pub/linux/utils/util-linux/>

Kmod
----

- <https://www.kernel.org/pub/linux/utils/kernel/kmod/>
- <https://git.kernel.org/pub/scm/utils/kernel/kmod/kmod.git>

Ksymoops
--------

- <https://www.kernel.org/pub/linux/utils/kernel/ksymoops/v2.4/>

Mkinitrd
--------

- <https://code.launchpad.net/initrd-tools/main>

E2fsprogs
---------

- <http://prdownloads.sourceforge.net/e2fsprogs/e2fsprogs-1.29.tar.gz>

JFSutils
--------

- <http://jfs.sourceforge.net/>

Reiserfsprogs
-------------

- <http://www.kernel.org/pub/linux/utils/fs/reiserfs/>

Xfsprogs
--------

- <ftp://oss.sgi.com/projects/xfs/>

Pcmciautils
-----------

- <https://www.kernel.org/pub/linux/utils/kernel/pcmcia/>

Quota-tools
-----------

- <http://sourceforge.net/projects/linuxquota/>


Microcodice Intel P6
--------------------

- <https://downloadcenter.intel.com/>

udev
----

- <http://www.freedesktop.org/software/systemd/man/udev.html>

FUSE
----

- <https://github.com/libfuse/libfuse/releases>

mcelog
------

- <http://www.mcelog.org/>

Rete
****

PPP
---

- <ftp://ftp.samba.org/pub/ppp/>


NFS-utils
---------

- <http://sourceforge.net/project/showfiles.php?group_id=14>

Iptables
--------

- <http://www.iptables.org/downloads.html>

Ip-route2
---------

- <https://www.kernel.org/pub/linux/utils/net/iproute2/>

OProfile
--------

- <http://oprofile.sf.net/download/>

NFS-Utils
---------

- <http://nfs.sourceforge.net/>

Documentazione del kernel
*************************

Sphinx
------

- <http://www.sphinx-doc.org/>
