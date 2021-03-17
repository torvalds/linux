.. include:: ../disclaimer-ita.rst

:Original: :ref:`Documentation/process/adding-syscalls.rst <addsyscalls>`
:Translator: Federico Vaga <federico.vaga@vaga.pv.it>

.. _it_addsyscalls:

Aggiungere una nuova chiamata di sistema
========================================

Questo documento descrive quello che è necessario sapere per aggiungere
nuove chiamate di sistema al kernel Linux; questo è da considerarsi come
un'aggiunta ai soliti consigli su come proporre nuove modifiche
:ref:`Documentation/translations/it_IT/process/submitting-patches.rst <it_submittingpatches>`.


Alternative alle chiamate di sistema
------------------------------------

La prima considerazione da fare quando si aggiunge una nuova chiamata di
sistema è quella di valutare le alternative.  Nonostante le chiamate di sistema
siano il punto di interazione fra spazio utente e kernel più tradizionale ed
ovvio, esistono altre possibilità - scegliete quella che meglio si adatta alle
vostra interfaccia.

 - Se le operazioni coinvolte possono rassomigliare a quelle di un filesystem,
   allora potrebbe avere molto più senso la creazione di un nuovo filesystem o
   dispositivo.  Inoltre, questo rende più facile incapsulare la nuova
   funzionalità in un modulo kernel piuttosto che essere sviluppata nel cuore
   del kernel.

     - Se la nuova funzionalità prevede operazioni dove il kernel notifica
       lo spazio utente su un avvenimento, allora restituire un descrittore
       di file all'oggetto corrispondente permette allo spazio utente di
       utilizzare ``poll``/``select``/``epoll`` per ricevere quelle notifiche.
     - Tuttavia, le operazioni che non si sposano bene con operazioni tipo
       :manpage:`read(2)`/:manpage:`write(2)` dovrebbero essere implementate
       come chiamate :manpage:`ioctl(2)`, il che potrebbe portare ad un'API in
       un qualche modo opaca.

 - Se dovete esporre solo delle informazioni sul sistema, un nuovo nodo in
   sysfs (vedere ``Documentation/filesystems/sysfs.rst``) o
   in procfs potrebbe essere sufficiente.  Tuttavia, l'accesso a questi
   meccanismi richiede che il filesystem sia montato, il che potrebbe non
   essere sempre vero (per esempio, in ambienti come namespace/sandbox/chroot).
   Evitate d'aggiungere nuove API in debugfs perché questo non viene
   considerata un'interfaccia di 'produzione' verso lo spazio utente.
 - Se l'operazione è specifica ad un particolare file o descrittore, allora
   potrebbe essere appropriata l'aggiunta di un comando :manpage:`fcntl(2)`.
   Tuttavia, :manpage:`fcntl(2)` è una chiamata di sistema multiplatrice che
   nasconde una notevole complessità, quindi è ottima solo quando la nuova
   funzione assomiglia a quelle già esistenti in :manpage:`fcntl(2)`, oppure
   la nuova funzionalità è veramente semplice (per esempio, leggere/scrivere
   un semplice flag associato ad un descrittore di file).
 - Se l'operazione è specifica ad un particolare processo, allora
   potrebbe essere appropriata l'aggiunta di un comando :manpage:`prctl(2)`.
   Come per :manpage:`fcntl(2)`, questa chiamata di sistema è un complesso
   multiplatore quindi è meglio usarlo per cose molto simili a quelle esistenti
   nel comando ``prctl`` oppure per leggere/scrivere un semplice flag relativo
   al processo.


Progettare l'API: pianificare le estensioni
-------------------------------------------

Una nuova chiamata di sistema diventerà parte dell'API del kernel, e
dev'essere supportata per un periodo indefinito.  Per questo, è davvero
un'ottima idea quella di discutere apertamente l'interfaccia sulla lista
di discussione del kernel, ed è altrettanto importante pianificarne eventuali
estensioni future.

(Nella tabella delle chiamate di sistema sono disseminati esempi dove questo
non fu fatto, assieme ai corrispondenti aggiornamenti -
``eventfd``/``eventfd2``, ``dup2``/``dup3``, ``inotify_init``/``inotify_init1``,
``pipe``/``pipe2``, ``renameat``/``renameat2`` --quindi imparate dalla storia
del kernel e pianificate le estensioni fin dall'inizio)

Per semplici chiamate di sistema che accettano solo un paio di argomenti,
il modo migliore di permettere l'estensibilità è quello di includere un
argomento *flags* alla chiamata di sistema.  Per assicurarsi che i programmi
dello spazio utente possano usare in sicurezza *flags* con diverse versioni
del kernel, verificate se *flags* contiene un qualsiasi valore sconosciuto,
in qual caso rifiutate la chiamata di sistema (con ``EINVAL``)::

    if (flags & ~(THING_FLAG1 | THING_FLAG2 | THING_FLAG3))
        return -EINVAL;

(Se *flags* non viene ancora utilizzato, verificate che l'argomento sia zero)

Per chiamate di sistema più sofisticate che coinvolgono un numero più grande di
argomenti, il modo migliore è quello di incapsularne la maggior parte in una
struttura dati che verrà passata per puntatore.  Questa struttura potrà
funzionare con future estensioni includendo un campo *size*::

    struct xyzzy_params {
        u32 size; /* userspace sets p->size = sizeof(struct xyzzy_params) */
        u32 param_1;
        u64 param_2;
        u64 param_3;
    };

Fintanto che un qualsiasi campo nuovo, diciamo ``param_4``, è progettato per
offrire il comportamento precedente quando vale zero, allora questo permetterà
di gestire un conflitto di versione in entrambe le direzioni:

 - un vecchio kernel può gestire l'accesso di una versione moderna di un
   programma in spazio utente verificando che la memoria oltre la dimensione
   della struttura dati attesa sia zero (in pratica verificare che
   ``param_4 == 0``).
 - un nuovo kernel può gestire l'accesso di una versione vecchia di un
   programma in spazio utente estendendo la struttura dati con zeri (in pratica
   ``param_4 = 0``).

Vedere :manpage:`perf_event_open(2)` e la funzione ``perf_copy_attr()`` (in
``kernel/events/core.c``) per un esempio pratico di questo approccio.


Progettare l'API: altre considerazioni
--------------------------------------

Se la vostra nuova chiamata di sistema permette allo spazio utente di fare
riferimento ad un oggetto del kernel, allora questa dovrebbe usare un
descrittore di file per accesso all'oggetto - non inventatevi nuovi tipi di
accesso da spazio utente quando il kernel ha già dei meccanismi e una semantica
ben definita per utilizzare i descrittori di file.

Se la vostra nuova chiamata di sistema :manpage:`xyzzy(2)` ritorna un nuovo
descrittore di file, allora l'argomento *flags* dovrebbe includere un valore
equivalente a ``O_CLOEXEC`` per i nuovi descrittori.  Questo rende possibile,
nello spazio utente, la chiusura della finestra temporale fra le chiamate a
``xyzzy()`` e ``fcntl(fd, F_SETFD, FD_CLOEXEC)``, dove un inaspettato
``fork()`` o ``execve()`` potrebbe trasferire il descrittore al programma
eseguito (Comunque, resistete alla tentazione di riutilizzare il valore di
``O_CLOEXEC`` dato che è specifico dell'architettura e fa parte di una
enumerazione di flag ``O_*`` che è abbastanza ricca).

Se la vostra nuova chiamata di sistema ritorna un nuovo descrittore di file,
dovreste considerare che significato avrà l'uso delle chiamate di sistema
della famiglia di :manpage:`poll(2)`. Rendere un descrittore di file pronto
per la lettura o la scrittura è il tipico modo del kernel per notificare lo
spazio utente circa un evento associato all'oggetto del kernel.

Se la vostra nuova chiamata di sistema :manpage:`xyzzy(2)` ha un argomento
che è il percorso ad un file::

    int sys_xyzzy(const char __user *path, ..., unsigned int flags);

dovreste anche considerare se non sia più appropriata una versione
:manpage:`xyzzyat(2)`::

    int sys_xyzzyat(int dfd, const char __user *path, ..., unsigned int flags);

Questo permette più flessibilità su come lo spazio utente specificherà il file
in questione; in particolare, permette allo spazio utente di richiedere la
funzionalità su un descrittore di file già aperto utilizzando il *flag*
``AT_EMPTY_PATH``, in pratica otterremmo gratuitamente l'operazione
:manpage:`fxyzzy(3)`::

 - xyzzyat(AT_FDCWD, path, ..., 0) is equivalent to xyzzy(path,...)
 - xyzzyat(fd, "", ..., AT_EMPTY_PATH) is equivalent to fxyzzy(fd, ...)

(Per maggiori dettagli sulla logica delle chiamate \*at(), leggete la pagina
man :manpage:`openat(2)`; per un esempio di AT_EMPTY_PATH, leggere la pagina
man :manpage:`fstatat(2)`).

Se la vostra nuova chiamata di sistema :manpage:`xyzzy(2)` prevede un parametro
per descrivere uno scostamento all'interno di un file, usate ``loff_t`` come
tipo cosicché scostamenti a 64-bit potranno essere supportati anche su
architetture a 32-bit.

Se la vostra nuova chiamata di sistema :manpage:`xyzzy(2)` prevede l'uso di
funzioni riservate, allora dev'essere gestita da un opportuno bit di privilegio
(verificato con una chiamata a ``capable()``), come descritto nella pagina man
:manpage:`capabilities(7)`.  Scegliete un bit di privilegio già esistente per
gestire la funzionalità associata, ma evitate la combinazione di diverse
funzionalità vagamente collegate dietro lo stesso bit, in quanto va contro il
principio di *capabilities* di separare i poteri di root.  In particolare,
evitate di aggiungere nuovi usi al fin-troppo-generico privilegio
``CAP_SYS_ADMIN``.

Se la vostra nuova chiamata di sistema :manpage:`xyzzy(2)` manipola altri
processi oltre a quello chiamato, allora dovrebbe essere limitata (usando
la chiamata ``ptrace_may_access()``) di modo che solo un processo chiamante
con gli stessi permessi del processo in oggetto, o con i necessari privilegi,
possa manipolarlo.

Infine, state attenti che in alcune architetture non-x86 la vita delle chiamate
di sistema con argomenti a 64-bit viene semplificata se questi argomenti
ricadono in posizioni dispari (pratica, i parametri 1, 3, 5); questo permette
l'uso di coppie contigue di registri a 32-bit.  (Questo non conta se gli
argomenti sono parte di una struttura dati che viene passata per puntatore).


Proporre l'API
--------------

Al fine di rendere le nuove chiamate di sistema di facile revisione, è meglio
che dividiate le modifiche i pezzi separati.  Questi dovrebbero includere
almeno le seguenti voci in *commit* distinti (ognuno dei quali sarà descritto
più avanti):

 - l'essenza dell'implementazione della chiamata di sistema, con i prototipi,
   i numeri generici, le modifiche al Kconfig e l'implementazione *stub* di
   ripiego.
 - preparare la nuova chiamata di sistema per un'architettura specifica,
   solitamente x86 (ovvero tutti: x86_64, x86_32 e x32).
 - un programma di auto-verifica da mettere in ``tools/testing/selftests/``
   che mostri l'uso della chiamata di sistema.
 - una bozza di pagina man per la nuova chiamata di sistema. Può essere
   scritta nell'email di presentazione, oppure come modifica vera e propria
   al repositorio delle pagine man.

Le proposte di nuove chiamate di sistema, come ogni altro modifica all'API del
kernel, deve essere sottomessa alla lista di discussione
linux-api@vger.kernel.org.


Implementazione di chiamate di sistema generiche
------------------------------------------------

Il principale punto d'accesso alla vostra nuova chiamata di sistema
:manpage:`xyzzy(2)` verrà chiamato ``sys_xyzzy()``; ma, piuttosto che in modo
esplicito, lo aggiungerete tramite la macro ``SYSCALL_DEFINEn``. La 'n'
indica il numero di argomenti della chiamata di sistema; la macro ha come
argomento il nome della chiamata di sistema, seguito dalle coppie (tipo, nome)
per definire i suoi parametri.  L'uso di questa macro permette di avere
i metadati della nuova chiamata di sistema disponibili anche per altri
strumenti.

Il nuovo punto d'accesso necessita anche del suo prototipo di funzione in
``include/linux/syscalls.h``, marcato come asmlinkage di modo da abbinargli
il modo in cui quelle chiamate di sistema verranno invocate::

    asmlinkage long sys_xyzzy(...);

Alcune architetture (per esempio x86) hanno le loro specifiche tabelle di
chiamate di sistema (syscall), ma molte altre architetture condividono una
tabella comune di syscall. Aggiungete alla lista generica la vostra nuova
chiamata di sistema aggiungendo un nuovo elemento alla lista in
``include/uapi/asm-generic/unistd.h``::

    #define __NR_xyzzy 292
    __SYSCALL(__NR_xyzzy, sys_xyzzy)

Aggiornate anche il contatore __NR_syscalls di modo che sia coerente con
l'aggiunta della nuove chiamate di sistema; va notato che se più di una nuova
chiamata di sistema viene aggiunga nella stessa finestra di sviluppo, il numero
della vostra nuova syscall potrebbe essere aggiustato al fine di risolvere i
conflitti.

Il file ``kernel/sys_ni.c`` fornisce le implementazioni *stub* di ripiego che
ritornano ``-ENOSYS``.  Aggiungete la vostra nuova chiamata di sistema anche
qui::

    COND_SYSCALL(xyzzy);

La vostra nuova funzionalità del kernel, e la chiamata di sistema che la
controlla, dovrebbero essere opzionali. Quindi, aggiungete un'opzione
``CONFIG`` (solitamente in ``init/Kconfig``).  Come al solito per le nuove
opzioni ``CONFIG``:

 - Includete una descrizione della nuova funzionalità e della chiamata di
   sistema che la controlla.
 - Rendete l'opzione dipendente da EXPERT se dev'essere nascosta agli utenti
   normali.
 - Nel Makefile, rendere tutti i nuovi file sorgenti, che implementano la
   nuova funzionalità, dipendenti dall'opzione CONFIG (per esempio
   ``obj-$(CONFIG_XYZZY_SYSCALL) += xyzzy.o``).
 - Controllate due volte che sia possibile generare il kernel con la nuova
   opzione CONFIG disabilitata.

Per riassumere, vi serve un *commit* che includa:

 - un'opzione ``CONFIG``per la nuova funzione, normalmente in ``init/Kconfig``
 - ``SYSCALL_DEFINEn(xyzzy, ...)`` per il punto d'accesso
 - il corrispondente prototipo in ``include/linux/syscalls.h``
 - un elemento nella tabella generica in ``include/uapi/asm-generic/unistd.h``
 - *stub* di ripiego in ``kernel/sys_ni.c``


Implementazione delle chiamate di sistema x86
---------------------------------------------

Per collegare la vostra nuova chiamate di sistema alle piattaforme x86,
dovete aggiornate la tabella principale di syscall.  Assumendo che la vostra
nuova chiamata di sistema non sia particolarmente speciale (vedere sotto),
dovete aggiungere un elemento *common* (per x86_64 e x32) in
arch/x86/entry/syscalls/syscall_64.tbl::

    333   common   xyzzy     sys_xyzzy

e un elemento per *i386* ``arch/x86/entry/syscalls/syscall_32.tbl``::

    380   i386     xyzzy     sys_xyzzy

Ancora una volta, questi numeri potrebbero essere cambiati se generano
conflitti durante la finestra di integrazione.


Chiamate di sistema compatibili (generico)
------------------------------------------

Per molte chiamate di sistema, la stessa implementazione a 64-bit può essere
invocata anche quando il programma in spazio utente è a 32-bit; anche se la
chiamata di sistema include esplicitamente un puntatore, questo viene gestito
in modo trasparente.

Tuttavia, ci sono un paio di situazione dove diventa necessario avere un
livello di gestione della compatibilità per risolvere le differenze di
dimensioni fra 32-bit e 64-bit.

Il primo caso è quando un kernel a 64-bit supporta anche programmi in spazio
utente a 32-bit, perciò dovrà ispezionare aree della memoria (``__user``) che
potrebbero contenere valori a 32-bit o a 64-bit.  In particolar modo, questo
è necessario quando un argomento di una chiamata di sistema è:

 - un puntatore ad un puntatore
 - un puntatore ad una struttura dati contenente a sua volta un puntatore
   ( ad esempio ``struct iovec __user *``)
 - un puntatore ad un tipo intero di dimensione variabile (``time_t``,
   ``off_t``, ``long``, ...)
 - un puntatore ad una struttura dati contenente un tipo intero di dimensione
   variabile.

Il secondo caso che richiede un livello di gestione della compatibilità è
quando uno degli argomenti di una chiamata a sistema è esplicitamente un tipo
a 64-bit anche su architetture a 32-bit, per esempio ``loff_t`` o ``__u64``.
In questo caso, un valore che arriva ad un kernel a 64-bit da un'applicazione
a 32-bit verrà diviso in due valori a 32-bit che dovranno essere riassemblati
in questo livello di compatibilità.

(Da notare che non serve questo livello di compatibilità per argomenti che
sono puntatori ad un tipo esplicitamente a 64-bit; per esempio, in
:manpage:`splice(2)` l'argomento di tipo ``loff_t __user *`` non necessita
di una chiamata di sistema ``compat_``)

La versione compatibile della nostra chiamata di sistema si chiamerà
``compat_sys_xyzzy()``, e viene aggiunta utilizzando la macro
``COMPAT_SYSCALL_DEFINEn()`` (simile a SYSCALL_DEFINEn).  Questa versione
dell'implementazione è parte del kernel a 64-bit ma accetta parametri a 32-bit
che trasformerà secondo le necessità (tipicamente, la versione
``compat_sys_`` converte questi valori nello loro corrispondente a 64-bit e
può chiamare la versione ``sys_`` oppure invocare una funzione che implementa
le parti comuni).

Il punto d'accesso *compat* deve avere il corrispondente prototipo di funzione
in ``include/linux/compat.h``, marcato come asmlinkage di modo da abbinargli
il modo in cui quelle chiamate di sistema verranno invocate::

    asmlinkage long compat_sys_xyzzy(...);

Se la chiamata di sistema prevede una struttura dati organizzata in modo
diverso per sistemi a 32-bit e per quelli a 64-bit, diciamo
``struct xyzzy_args``, allora il file d'intestazione
``then the include/linux/compat.h`` deve includere la sua versione
*compatibile* (``struct compat_xyzzy_args``); ogni variabile con
dimensione variabile deve avere il proprio tipo ``compat_`` corrispondente
a quello in ``struct xyzzy_args``.  La funzione ``compat_sys_xyzzy()``
può usare la struttura ``compat_`` per analizzare gli argomenti ricevuti
da una chiamata a 32-bit.

Per esempio, se avete i seguenti campi::

    struct xyzzy_args {
        const char __user *ptr;
        __kernel_long_t varying_val;
        u64 fixed_val;
        /* ... */
    };

nella struttura ``struct xyzzy_args``, allora la struttura
``struct compat_xyzzy_args`` dovrebbe avere::

    struct compat_xyzzy_args {
        compat_uptr_t ptr;
        compat_long_t varying_val;
        u64 fixed_val;
        /* ... */
    };

La lista generica delle chiamate di sistema ha bisogno di essere
aggiustata al fine di permettere l'uso della versione *compatibile*;
la voce in ``include/uapi/asm-generic/unistd.h`` dovrebbero usare
``__SC_COMP`` piuttosto di ``__SYSCALL``::

    #define __NR_xyzzy 292
    __SC_COMP(__NR_xyzzy, sys_xyzzy, compat_sys_xyzzy)

Riassumendo, vi serve:

 - un ``COMPAT_SYSCALL_DEFINEn(xyzzy, ...)`` per il punto d'accesso
   *compatibile*
 - un prototipo in ``include/linux/compat.h``
 - (se necessario) una struttura di compatibilità a 32-bit in
   ``include/linux/compat.h``
 - una voce ``__SC_COMP``, e non ``__SYSCALL``, in
   ``include/uapi/asm-generic/unistd.h``

Compatibilità delle chiamate di sistema (x86)
---------------------------------------------

Per collegare una chiamata di sistema, su un'architettura x86, con la sua
versione *compatibile*, è necessario aggiustare la voce nella tabella
delle syscall.

Per prima cosa, la voce in ``arch/x86/entry/syscalls/syscall_32.tbl`` prende
un argomento aggiuntivo per indicare che un programma in spazio utente
a 32-bit, eseguito su un kernel a 64-bit, dovrebbe accedere tramite il punto
d'accesso compatibile::

    380   i386     xyzzy     sys_xyzzy    __ia32_compat_sys_xyzzy

Secondo, dovete capire cosa dovrebbe succedere alla nuova chiamata di sistema
per la versione dell'ABI x32.  Qui C'è una scelta da fare: gli argomenti
possono corrisponde alla versione a 64-bit o a quella a 32-bit.

Se c'è un puntatore ad un puntatore, la decisione è semplice: x32 è ILP32,
quindi gli argomenti dovrebbero corrispondere a quelli a 32-bit, e la voce in
``arch/x86/entry/syscalls/syscall_64.tbl`` sarà divisa cosicché i programmi
x32 eseguano la chiamata *compatibile*::

    333   64       xyzzy     sys_xyzzy
    ...
    555   x32      xyzzy     __x32_compat_sys_xyzzy

Se non ci sono puntatori, allora è preferibile riutilizzare la chiamata di
sistema a 64-bit per l'ABI x32 (e di conseguenza la voce in
arch/x86/entry/syscalls/syscall_64.tbl rimane immutata).

In ambo i casi, dovreste verificare che i tipi usati dagli argomenti
abbiano un'esatta corrispondenza da x32 (-mx32) al loro equivalente a
32-bit (-m32) o 64-bit (-m64).


Chiamate di sistema che ritornano altrove
-----------------------------------------

Nella maggior parte delle chiamate di sistema, al termine della loro
esecuzione, i programmi in spazio utente riprendono esattamente dal punto
in cui si erano interrotti -- quindi dall'istruzione successiva, con lo
stesso *stack* e con la maggior parte del registri com'erano stati
lasciati prima della chiamata di sistema, e anche con la stessa memoria
virtuale.

Tuttavia, alcune chiamata di sistema fanno le cose in modo differente.
Potrebbero ritornare ad un punto diverso (``rt_sigreturn``) o cambiare
la memoria in spazio utente (``fork``/``vfork``/``clone``) o perfino
l'architettura del programma (``execve``/``execveat``).

Per permettere tutto ciò, l'implementazione nel kernel di questo tipo di
chiamate di sistema potrebbero dover salvare e ripristinare registri
aggiuntivi nello *stack* del kernel, permettendo così un controllo completo
su dove e come l'esecuzione dovrà continuare dopo l'esecuzione della
chiamata di sistema.

Queste saranno specifiche per ogni architettura, ma tipicamente si definiscono
dei punti d'accesso in *assembly* per salvare/ripristinare i registri
aggiuntivi e quindi chiamare il vero punto d'accesso per la chiamata di
sistema.

Per l'architettura x86_64, questo è implementato come un punto d'accesso
``stub_xyzzy`` in ``arch/x86/entry/entry_64.S``, e la voce nella tabella
di syscall (``arch/x86/entry/syscalls/syscall_64.tbl``) verrà corretta di
conseguenza::

    333   common   xyzzy     stub_xyzzy

L'equivalente per programmi a 32-bit eseguiti su un kernel a 64-bit viene
normalmente chiamato ``stub32_xyzzy`` e implementato in
``arch/x86/entry/entry_64_compat.S`` con la corrispondente voce nella tabella
di syscall ``arch/x86/entry/syscalls/syscall_32.tbl`` corretta nel
seguente modo::

    380   i386     xyzzy     sys_xyzzy    stub32_xyzzy

Se una chiamata di sistema necessita di un livello di compatibilità (come
nella sezione precedente), allora la versione ``stub32_`` deve invocare
la versione ``compat_sys_`` piuttosto che quella nativa a 64-bit.  In aggiunta,
se l'implementazione dell'ABI x32 è diversa da quella x86_64, allora la sua
voce nella tabella di syscall dovrà chiamare uno *stub* che invoca la versione
``compat_sys_``,

Per completezza, sarebbe carino impostare una mappatura cosicché
*user-mode* Linux (UML) continui a funzionare -- la sua tabella di syscall
farà riferimento a stub_xyzzy, ma UML non include l'implementazione
in ``arch/x86/entry/entry_64.S`` (perché UML simula i registri eccetera).
Correggerlo è semplice, basta aggiungere una #define in
``arch/x86/um/sys_call_table_64.c``::

    #define stub_xyzzy sys_xyzzy


Altri dettagli
--------------

La maggior parte dei kernel tratta le chiamate di sistema allo stesso modo,
ma possono esserci rare eccezioni per le quali potrebbe essere necessario
l'aggiornamento della vostra chiamata di sistema.

Il sotto-sistema di controllo (*audit subsystem*) è uno di questi casi
speciali; esso include (per architettura) funzioni che classificano alcuni
tipi di chiamate di sistema -- in particolare apertura dei file
(``open``/``openat``), esecuzione dei programmi (``execve``/``exeveat``)
oppure multiplatori di socket (``socketcall``). Se la vostra nuova chiamata
di sistema è simile ad una di queste, allora il sistema di controllo dovrebbe
essere aggiornato.

Più in generale, se esiste una chiamata di sistema che è simile alla vostra,
vale la pena fare una ricerca con ``grep`` su tutto il kernel per la chiamata
di sistema esistente per verificare che non ci siano altri casi speciali.


Verifica
--------

Una nuova chiamata di sistema dev'essere, ovviamente, provata; è utile fornire
ai revisori un programma in spazio utente che mostri l'uso della chiamata di
sistema.  Un buon modo per combinare queste cose è quello di aggiungere un
semplice programma di auto-verifica in una nuova cartella in
``tools/testing/selftests/``.

Per una nuova chiamata di sistema, ovviamente, non ci sarà alcuna funzione
in libc e quindi il programma di verifica dovrà invocarla usando ``syscall()``;
inoltre, se la nuova chiamata di sistema prevede un nuova struttura dati
visibile in spazio utente, il file d'intestazione necessario dev'essere
installato al fine di compilare il programma.

Assicuratevi che il programma di auto-verifica possa essere eseguito
correttamente su tutte le architetture supportate.  Per esempio, verificate che
funzioni quando viene compilato per x86_64 (-m64), x86_32 (-m32) e x32 (-mx32).

Al fine di una più meticolosa ed estesa verifica della nuova funzionalità,
dovreste considerare l'aggiunta di nuove verifica al progetto 'Linux Test',
oppure al progetto xfstests per cambiamenti relativi al filesystem.

 - https://linux-test-project.github.io/
 - git://git.kernel.org/pub/scm/fs/xfs/xfstests-dev.git


Pagine man
----------

Tutte le nuove chiamate di sistema dovrebbero avere una pagina man completa,
idealmente usando i marcatori groff, ma anche il puro testo può andare.  Se
state usando groff, è utile che includiate nella email di presentazione una
versione già convertita in formato ASCII: semplificherà la vita dei revisori.

Le pagine man dovrebbero essere in copia-conoscenza verso
linux-man@vger.kernel.org
Per maggiori dettagli, leggere
https://www.kernel.org/doc/man-pages/patches.html


Non invocate chiamate di sistema dal kernel
-------------------------------------------

Le chiamate di sistema sono, come già detto prima, punti di interazione fra
lo spazio utente e il kernel.  Perciò, le chiamate di sistema come
``sys_xyzzy()`` o ``compat_sys_xyzzy()`` dovrebbero essere chiamate solo dallo
spazio utente attraverso la tabella syscall, ma non da nessun altro punto nel
kernel.  Se la nuova funzionalità è utile all'interno del kernel, per esempio
dev'essere condivisa fra una vecchia e una nuova chiamata di sistema o
dev'essere utilizzata da una chiamata di sistema e la sua variante compatibile,
allora dev'essere implementata come una funzione di supporto
(*helper function*) (per esempio ``kern_xyzzy()``).  Questa funzione potrà
essere chiamata dallo *stub* (``sys_xyzzy()``), dalla variante compatibile
(``compat_sys_xyzzy()``), e/o da altri parti del kernel.

Sui sistemi x86 a 64-bit, a partire dalla versione v4.17 è un requisito
fondamentale quello di non invocare chiamate di sistema all'interno del kernel.
Esso usa una diversa convenzione per l'invocazione di chiamate di sistema dove
``struct pt_regs`` viene decodificata al volo in una funzione che racchiude
la chiamata di sistema la quale verrà eseguita successivamente.
Questo significa che verranno passati solo i parametri che sono davvero
necessari ad una specifica chiamata di sistema, invece che riempire ogni volta
6 registri del processore con contenuti presi dallo spazio utente (potrebbe
causare seri problemi nella sequenza di chiamate).

Inoltre, le regole su come i dati possano essere usati potrebbero differire
fra il kernel e l'utente.  Questo è un altro motivo per cui invocare
``sys_xyzzy()`` è generalmente una brutta idea.

Eccezioni a questa regola vengono accettate solo per funzioni d'architetture
che surclassano quelle generiche, per funzioni d'architettura di compatibilità,
o per altro codice in arch/


Riferimenti e fonti
-------------------

 - Articolo di Michael Kerris su LWN sull'uso dell'argomento flags nelle
   chiamate di sistema: https://lwn.net/Articles/585415/
 - Articolo di Michael Kerris su LWN su come gestire flag sconosciuti in
   una chiamata di sistema: https://lwn.net/Articles/588444/
 - Articolo di Jake Edge su LWN che descrive i limiti degli argomenti a 64-bit
   delle chiamate di sistema: https://lwn.net/Articles/311630/
 - Una coppia di articoli di David Drysdale che descrivono i dettagli del
   percorso implementativo di una chiamata di sistema per la versione v3.14:

    - https://lwn.net/Articles/604287/
    - https://lwn.net/Articles/604515/

 - Requisiti specifici alle architetture sono discussi nella pagina man
   :manpage:`syscall(2)` :
   http://man7.org/linux/man-pages/man2/syscall.2.html#NOTES
 - Collezione di email di Linux Torvalds sui problemi relativi a ``ioctl()``:
   http://yarchive.net/comp/linux/ioctl.html
 - "Come non inventare interfacce del kernel", Arnd Bergmann,
   http://www.ukuug.org/events/linux2007/2007/papers/Bergmann.pdf
 - Articolo di Michael Kerris su LWN sull'evitare nuovi usi di CAP_SYS_ADMIN:
   https://lwn.net/Articles/486306/
 - Raccomandazioni da Andrew Morton circa il fatto che tutte le informazioni
   su una nuova chiamata di sistema dovrebbero essere contenute nello stesso
   filone di discussione di email: https://lkml.org/lkml/2014/7/24/641
 - Raccomandazioni da Michael Kerrisk circa il fatto che le nuove chiamate di
   sistema dovrebbero avere una pagina man: https://lkml.org/lkml/2014/6/13/309
 - Consigli da Thomas Gleixner sul fatto che il collegamento all'architettura
   x86 dovrebbe avvenire in un *commit* differente:
   https://lkml.org/lkml/2014/11/19/254
 - Consigli da Greg Kroah-Hartman circa la bontà d'avere una pagina man e un
   programma di auto-verifica per le nuove chiamate di sistema:
   https://lkml.org/lkml/2014/3/19/710
 - Discussione di Michael Kerrisk sulle nuove chiamate di sistema contro
   le estensioni :manpage:`prctl(2)`: https://lkml.org/lkml/2014/6/3/411
 - Consigli da Ingo Molnar che le chiamate di sistema con più argomenti
   dovrebbero incapsularli in una struttura che includa un argomento
   *size* per garantire l'estensibilità futura:
   https://lkml.org/lkml/2015/7/30/117
 - Un certo numero di casi strani emersi dall'uso (riuso) dei flag O_*:

    - commit 75069f2b5bfb ("vfs: renumber FMODE_NONOTIFY and add to uniqueness
      check")
    - commit 12ed2e36c98a ("fanotify: FMODE_NONOTIFY and __O_SYNC in sparc
      conflict")
    - commit bb458c644a59 ("Safer ABI for O_TMPFILE")

 - Discussion from Matthew Wilcox about restrictions on 64-bit arguments:
   https://lkml.org/lkml/2008/12/12/187
 - Raccomandazioni da Greg Kroah-Hartman sul fatto che i flag sconosciuti dovrebbero
   essere controllati: https://lkml.org/lkml/2014/7/17/577
 - Raccomandazioni da Linus Torvalds che le chiamate di sistema x32 dovrebbero
   favorire la compatibilità con le versioni a 64-bit piuttosto che quelle a 32-bit:
   https://lkml.org/lkml/2011/8/31/244
