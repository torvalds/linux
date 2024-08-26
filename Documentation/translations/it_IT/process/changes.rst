.. include:: ../disclaimer-ita.rst

:Original: :ref:`Documentation/process/changes.rst <changes>`
:Translator: Federico Vaga <federico.vaga@vaga.pv.it>

.. _it_changes:

Requisiti minimi per compilare il kernel
++++++++++++++++++++++++++++++++++++++++

Introduzione
============

Questo documento fornisce una lista dei software necessari per eseguire questa
versione del kernel.

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
GNU C                  5.1                gcc --version
Clang/LLVM (optional)  13.0.0             clang --version
Rust (opzionale)       1.76.0             rustc --version
bindgen (opzionale)    0.65.1             bindgen --version
GNU make               3.81               make --version
bash                   4.2                bash --version
binutils               2.25               ld -v
flex                   2.5.35             flex --version
bison                  2.0                bison --version
pahole                 1.16               pahole --version
util-linux             2.10o              mount --version
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
udev                   081                udevd --version
grub                   0.93               grub --version || grub-install --version
mcelog                 0.6                mcelog --version
iptables               1.4.2              iptables -V
openssl & libcrypto    1.0.0              openssl version
bc                     1.06.95            bc --version
Sphinx\ [#f1]_         2.4.4              sphinx-build --version
cpio                   any                cpio --version
GNU tar                1.28               tar --version
gtags (opzionale)      6.6.5              gtags --version
====================== =================  ========================================

.. [#f1] Sphinx è necessario solo per produrre la documentazione del Kernel

Compilazione del kernel
***********************

GCC
---

La versione necessaria di gcc potrebbe variare a seconda del tipo di CPU nel
vostro calcolatore.

Clang/LLVM (opzionale)
----------------------

L'ultima versione di clang e *LLVM utils* (secondo `releases.llvm.org
<https://releases.llvm.org>`_) sono supportati per la generazione del
kernel. Non garantiamo che anche i rilasci più vecchi funzionino, inoltre
potremmo rimuovere gli espedienti che abbiamo implementato per farli
funzionare. Per maggiori informazioni
:ref:`Building Linux with Clang/LLVM <kbuild_llvm>`.

Make
----

Per compilare il kernel vi servirà GNU make 3.81 o successivo.

Bash
----
Per generare il kernel vengono usati alcuni script per bash.
Questo richiede bash 4.2 o successivo.

Binutils
--------

Per generare il kernel è necessario avere Binutils 2.25 o superiore.

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

pahole
------

Dalla versione 5.2, quando viene impostato CONFIG_DEBUG_INFO_BTF, il sistema di
compilazione genera BTF (BPF Type Format) a partire da DWARF per vmlinux. Più
tardi anche per i moduli. Questo richiede pahole v1.16 o successivo.

A seconda della distribuzione, lo si può trovare nei pacchetti 'dwarves' o
'pahole'. Oppure lo si può trovare qui: https://fedorapeople.org/~acme/dwarves/.

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

Tar
---

GNU Tar è necessario per accedere ai file d'intestazione del kernel usando sysfs
(CONFIG_IKHEADERS)

gtags / GNU GLOBAL (opzionale)
------------------------------

Il programma GNU GLOBAL versione 6.6.5, o successiva, è necessario quando si
vuole eseguire ``make gtags`` e generare i relativi indici. Internamente si fa
uso del parametro gtags ``-C (--directory)`` che compare in questa versione.

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

Clang/LLVM
----------

- :ref:`Getting LLVM <getting_llvm>`.

Make
----

- <ftp://ftp.gnu.org/gnu/make/>

Bash
----

- <ftp://ftp.gnu.org/gnu/bash/>

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

- <https://www.kernel.org/pub/linux/kernel/people/tytso/e2fsprogs/>
- <https://git.kernel.org/pub/scm/fs/ext2/e2fsprogs.git/>

JFSutils
--------

- <https://jfs.sourceforge.net/>

Reiserfsprogs
-------------

- <https://git.kernel.org/pub/scm/linux/kernel/git/jeffm/reiserfsprogs.git/>

Xfsprogs
--------

- <https://git.kernel.org/pub/scm/fs/xfs/xfsprogs-dev.git>
- <https://www.kernel.org/pub/linux/utils/fs/xfs/xfsprogs/>

Pcmciautils
-----------

- <https://www.kernel.org/pub/linux/utils/kernel/pcmcia/>

Quota-tools
-----------

- <https://sourceforge.net/projects/linuxquota/>


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

- <https://www.mcelog.org/>

cpio
----

- <https://www.gnu.org/software/cpio/>

Rete
****

PPP
---

- <https://download.samba.org/pub/ppp/>
- <https://git.ozlabs.org/?p=ppp.git>
- <https://github.com/paulusmack/ppp/>


NFS-utils
---------

- <https://sourceforge.net/project/showfiles.php?group_id=14>
- <https://nfs.sourceforge.net/>

Iptables
--------

- <https://netfilter.org/projects/iptables/index.html>

Ip-route2
---------

- <https://www.kernel.org/pub/linux/utils/net/iproute2/>

OProfile
--------

- <https://oprofile.sf.net/download/>

Documentazione del kernel
*************************

Sphinx
------

- <http://www.sphinx-doc.org/>
