.. include:: ../disclaimer-ita.rst

.. note:: Per leggere la documentazione originale in inglese:
	  :ref:`Documentation/kernel-hacking/hacking.rst <kernel_hacking_hack>`

:Original: :ref:`Documentation/kernel-hacking/hacking.rst <kernel_hacking_hack>`
:Translator: Federico Vaga <federico.vaga@vaga.pv.it>

.. _it_kernel_hacking_hack:

=================================================
L'inaffidabile guida all'hacking del kernel Linux
=================================================

:Author: Rusty Russell

Introduzione
============

Benvenuto, gentile lettore, alla notevole ed inaffidabile guida all'hacking
del kernel Linux ad opera di Rusty. Questo documento descrive le procedure
più usate ed i concetti necessari per scrivere codice per il kernel: lo scopo
è di fornire ai programmatori C più esperti un manuale di base per sviluppo.
Eviterò dettagli implementativi: per questo abbiamo il codice,
ed ignorerò intere parti di alcune procedure.

Prima di leggere questa guida, sappiate che non ho mai voluto scriverla,
essendo esageratamente sotto qualificato, ma ho sempre voluto leggere
qualcosa di simile, e quindi questa era l'unica via. Spero che possa
crescere e diventare un compendio di buone pratiche, punti di partenza
e generiche informazioni.

Gli attori
==========

In qualsiasi momento ognuna delle CPU di un sistema può essere:

-  non associata ad alcun processo, servendo un'interruzione hardware;

-  non associata ad alcun processo, servendo un softirq o tasklet;

-  in esecuzione nello spazio kernel, associata ad un processo
   (contesto utente);

-  in esecuzione di un processo nello spazio utente;

Esiste un ordine fra questi casi. Gli ultimi due possono avvicendarsi (preempt)
l'un l'altro, ma a parte questo esiste una gerarchia rigida: ognuno di questi
può avvicendarsi solo ad uno di quelli sottostanti. Per esempio, mentre un
softirq è in esecuzione su d'una CPU, nessun altro softirq può avvicendarsi
nell'esecuzione, ma un'interruzione hardware può. Ciò nonostante, le altre CPU
del sistema operano indipendentemente.

Più avanti vedremo alcuni modi in cui dal contesto utente è possibile bloccare
le interruzioni, così da impedirne davvero il diritto di prelazione.

Contesto utente
---------------

Ci si trova nel contesto utente quando si arriva da una chiamata di sistema
od altre eccezioni: come nello spazio utente, altre procedure più importanti,
o le interruzioni, possono far valere il proprio diritto di prelazione sul
vostro processo. Potete sospendere l'esecuzione chiamando :c:func:`schedule()`.

.. note::

    Si è sempre in contesto utente quando un modulo viene caricato o rimosso,
    e durante le operazioni nello strato dei dispositivi a blocchi
    (*block layer*).

Nel contesto utente, il puntatore ``current`` (il quale indica il processo al
momento in esecuzione) è valido, e :c:func:`in_interrupt()`
(``include/linux/preempt.h``) è falsa.

.. warning::

    Attenzione che se avete la prelazione o i softirq disabilitati (vedere
    di seguito), :c:func:`in_interrupt()` ritornerà un falso positivo.

Interruzioni hardware (Hard IRQs)
---------------------------------

Temporizzatori, schede di rete e tastiere sono esempi di vero hardware
che possono produrre interruzioni in un qualsiasi momento. Il kernel esegue
i gestori d'interruzione che prestano un servizio all'hardware. Il kernel
garantisce che questi gestori non vengano mai interrotti: se una stessa
interruzione arriva, questa verrà accodata (o scartata).
Dato che durante la loro esecuzione le interruzioni vengono disabilitate,
i gestori d'interruzioni devono essere veloci: spesso si limitano
esclusivamente a notificare la presa in carico dell'interruzione,
programmare una 'interruzione software' per l'esecuzione e quindi terminare.

Potete dire d'essere in una interruzione hardware perché in_hardirq()
ritorna vero.

.. warning::

    Attenzione, questa ritornerà un falso positivo se le interruzioni
    sono disabilitate (vedere di seguito).

Contesto d'interruzione software: softirq e tasklet
---------------------------------------------------

Quando una chiamata di sistema sta per tornare allo spazio utente,
oppure un gestore d'interruzioni termina, qualsiasi 'interruzione software'
marcata come pendente (solitamente da un'interruzione hardware) viene
eseguita (``kernel/softirq.c``).

La maggior parte del lavoro utile alla gestione di un'interruzione avviene qui.
All'inizio della transizione ai sistemi multiprocessore, c'erano solo i
cosiddetti 'bottom half' (BH), i quali non traevano alcun vantaggio da questi
sistemi. Non appena abbandonammo i computer raffazzonati con fiammiferi e
cicche, abbandonammo anche questa limitazione e migrammo alle interruzioni
software 'softirqs'.

Il file ``include/linux/interrupt.h`` elenca i differenti tipi di 'softirq'.
Un tipo di softirq molto importante è il timer (``include/linux/timer.h``):
potete programmarlo per far si che esegua funzioni dopo un determinato
periodo di tempo.

Dato che i softirq possono essere eseguiti simultaneamente su più di un
processore, spesso diventa estenuante l'averci a che fare. Per questa ragione,
i tasklet (``include/linux/interrupt.h``) vengo usati più di frequente:
possono essere registrati dinamicamente (il che significa che potete averne
quanti ne volete), e garantiscono che un qualsiasi tasklet verrà eseguito
solo su un processore alla volta, sebbene diversi tasklet possono essere
eseguiti simultaneamente.

.. warning::

    Il nome 'tasklet' è ingannevole: non hanno niente a che fare
    con i 'processi' ('tasks').

Potete determinate se siete in un softirq (o tasklet) utilizzando la
macro :c:func:`in_softirq()` (``include/linux/preempt.h``).

.. warning::

    State attenti che questa macro ritornerà un falso positivo
    se :ref:`bottom half lock <it_local_bh_disable>` è bloccato.

Alcune regole basilari
======================

Nessuna protezione della memoria
    Se corrompete la memoria, che sia in contesto utente o d'interruzione,
    la macchina si pianterà. Siete sicuri che quello che volete fare
    non possa essere fatto nello spazio utente?

Nessun numero in virgola mobile o MMX
    Il contesto della FPU non è salvato; anche se siete in contesto utente
    lo stato dell'FPU probabilmente non corrisponde a quello del processo
    corrente: vi incasinerete con lo stato di qualche altro processo. Se
    volete davvero usare la virgola mobile, allora dovrete salvare e recuperare
    lo stato dell'FPU (ed evitare cambi di contesto). Generalmente è una
    cattiva idea; usate l'aritmetica a virgola fissa.

Un limite rigido dello stack
    A seconda della configurazione del kernel lo stack è fra 3K e 6K per la
    maggior parte delle architetture a 32-bit; è di 14K per la maggior
    parte di quelle a 64-bit; e spesso è condiviso con le interruzioni,
    per cui non si può usare.
    Evitare profonde ricorsioni ad enormi array locali nello stack
    (allocateli dinamicamente).

Il kernel Linux è portabile
    Quindi mantenetelo tale. Il vostro codice dovrebbe essere a 64-bit ed
    indipendente dall'ordine dei byte (endianess) di un processore. Inoltre,
    dovreste minimizzare il codice specifico per un processore; per esempio
    il codice assembly dovrebbe essere incapsulato in modo pulito e minimizzato
    per facilitarne la migrazione. Generalmente questo codice dovrebbe essere
    limitato alla parte di kernel specifica per un'architettura.

ioctl: non scrivere nuove chiamate di sistema
=============================================

Una chiamata di sistema, generalmente, è scritta così::

    asmlinkage long sys_mycall(int arg)
    {
            return 0;
    }

Primo, nella maggior parte dei casi non volete creare nuove chiamate di
sistema.
Create un dispositivo a caratteri ed implementate l'appropriata chiamata ioctl.
Questo meccanismo è molto più flessibile delle chiamate di sistema: esso non
dev'essere dichiarato in tutte le architetture nei file
``include/asm/unistd.h`` e ``arch/kernel/entry.S``; inoltre, è improbabile
che questo venga accettato da Linus.

Se tutto quello che il vostro codice fa è leggere o scrivere alcuni parametri,
considerate l'implementazione di un'interfaccia :c:func:`sysfs()`.

All'interno di una ioctl vi trovate nel contesto utente di un processo. Quando
avviene un errore dovete ritornare un valore negativo di errno (consultate
``include/uapi/asm-generic/errno-base.h``,
``include/uapi/asm-generic/errno.h`` e ``include/linux/errno.h``), altrimenti
ritornate 0.

Dopo aver dormito dovreste verificare se ci sono stati dei segnali: il modo
Unix/Linux di gestire un segnale è di uscire temporaneamente dalla chiamata
di sistema con l'errore ``-ERESTARTSYS``. La chiamata di sistema ritornerà
al contesto utente, eseguirà il gestore del segnale e poi la vostra chiamata
di sistema riprenderà (a meno che l'utente non l'abbia disabilitata). Quindi,
dovreste essere pronti per continuare l'esecuzione, per esempio nel mezzo
della manipolazione di una struttura dati.

::

    if (signal_pending(current))
            return -ERESTARTSYS;

Se dovete eseguire dei calcoli molto lunghi: pensate allo spazio utente.
Se **davvero** volete farlo nel kernel ricordatevi di verificare periodicamente
se dovete *lasciare* il processore (ricordatevi che, per ogni processore, c'è
un sistema multi-processo senza diritto di prelazione).
Esempio::

    cond_resched(); /* Will sleep */

Una breve nota sulla progettazione delle interfacce: il motto dei sistemi
UNIX è "fornite meccanismi e non politiche"

La ricetta per uno stallo
=========================

Non è permesso invocare una procedura che potrebbe dormire, fanno eccezione
i seguenti casi:

-  Siete in un contesto utente.

-  Non trattenete alcun spinlock.

-  Avete abilitato le interruzioni (in realtà, Andy Kleen dice che
   lo schedulatore le abiliterà per voi, ma probabilmente questo non è quello
   che volete).

Da tener presente che alcune funzioni potrebbero dormire implicitamente:
le più comuni sono quelle per l'accesso allo spazio utente (\*_user) e
quelle per l'allocazione della memoria senza l'opzione ``GFP_ATOMIC``

Dovreste sempre compilare il kernel con l'opzione ``CONFIG_DEBUG_ATOMIC_SLEEP``
attiva, questa vi avviserà se infrangete una di queste regole.
Se **infrangete** le regole, allora potreste bloccare il vostro scatolotto.

Veramente.

Alcune delle procedure più comuni
=================================

:c:func:`printk()`
------------------

Definita in ``include/linux/printk.h``

:c:func:`printk()` fornisce messaggi alla console, dmesg, e al demone syslog.
Essa è utile per il debugging o per la notifica di errori; può essere
utilizzata anche all'interno del contesto d'interruzione, ma usatela con
cautela: una macchina che ha la propria console inondata da messaggi diventa
inutilizzabile. La funzione utilizza un formato stringa quasi compatibile con
la printf ANSI C, e la concatenazione di una stringa C come primo argomento
per indicare la "priorità"::

    printk(KERN_INFO "i = %u\n", i);

Consultate ``include/linux/kern_levels.h`` per gli altri valori ``KERN_``;
questi sono interpretati da syslog come livelli. Un caso speciale:
per stampare un indirizzo IP usate::

    __be32 ipaddress;
    printk(KERN_INFO "my ip: %pI4\n", &ipaddress);


:c:func:`printk()` utilizza un buffer interno di 1K e non s'accorge di
eventuali sforamenti. Accertatevi che vi basti.

.. note::

    Saprete di essere un vero hacker del kernel quando inizierete a digitare
    nei vostri programmi utenti le printf come se fossero printk :)

.. note::

    Un'altra nota a parte: la versione originale di Unix 6 aveva un commento
    sopra alla funzione printf: "Printf non dovrebbe essere usata per il
    chiacchiericcio". Dovreste seguire questo consiglio.

:c:func:`copy_to_user()` / :c:func:`copy_from_user()` / :c:func:`get_user()` / :c:func:`put_user()`
---------------------------------------------------------------------------------------------------

Definite in ``include/linux/uaccess.h`` / ``asm/uaccess.h``

**[DORMONO]**

:c:func:`put_user()` e :c:func:`get_user()` sono usate per ricevere ed
impostare singoli valori (come int, char, o long) da e verso lo spazio utente.
Un puntatore nello spazio utente non dovrebbe mai essere dereferenziato: i dati
dovrebbero essere copiati usando suddette procedure. Entrambe ritornano
``-EFAULT`` oppure 0.

:c:func:`copy_to_user()` e :c:func:`copy_from_user()` sono più generiche:
esse copiano una quantità arbitraria di dati da e verso lo spazio utente.

.. warning::

    Al contrario di:c:func:`put_user()` e :c:func:`get_user()`, queste
    funzioni ritornano la quantità di dati copiati (0 è comunque un successo).

[Sì, questa interfaccia mi imbarazza. La battaglia torna in auge anno
dopo anno. --RR]

Le funzioni potrebbero dormire implicitamente. Queste non dovrebbero mai essere
invocate fuori dal contesto utente (non ha senso), con le interruzioni
disabilitate, o con uno spinlock trattenuto.

:c:func:`kmalloc()`/:c:func:`kfree()`
-------------------------------------

Definite in ``include/linux/slab.h``

**[POTREBBERO DORMIRE: LEGGI SOTTO]**

Queste procedure sono utilizzate per la richiesta dinamica di un puntatore ad
un pezzo di memoria allineato, esattamente come malloc e free nello spazio
utente, ma :c:func:`kmalloc()` ha un argomento aggiuntivo per indicare alcune
opzioni. Le opzioni più importanti sono:

``GFP_KERNEL``
    Potrebbe dormire per librarare della memoria. L'opzione fornisce il modo
    più affidabile per allocare memoria, ma il suo uso è strettamente limitato
    allo spazio utente.

``GFP_ATOMIC``
    Non dorme. Meno affidabile di ``GFP_KERNEL``, ma può essere usata in un
    contesto d'interruzione. Dovreste avere **davvero** una buona strategia
    per la gestione degli errori in caso di mancanza di memoria.

``GFP_DMA``
    Alloca memoria per il DMA sul bus ISA nello spazio d'indirizzamento
    inferiore ai 16MB. Se non sapete cos'è allora non vi serve.
    Molto inaffidabile.

Se vedete un messaggio d'avviso per una funzione dormiente che viene chiamata
da un contesto errato, allora probabilmente avete usato una funzione
d'allocazione dormiente da un contesto d'interruzione senza ``GFP_ATOMIC``.
Dovreste correggerlo. Sbrigatevi, non cincischiate.

Se allocate almeno ``PAGE_SIZE``(``asm/page.h`` o ``asm/page_types.h``) byte,
considerate l'uso di :c:func:`__get_free_pages()` (``include/linux/gfp.h``).
Accetta un argomento che definisce l'ordine (0 per per la dimensione di una
pagine, 1 per una doppia pagina, 2 per quattro pagine, eccetra) e le stesse
opzioni d'allocazione viste precedentemente.

Se state allocando un numero di byte notevolemnte superiore ad una pagina
potete usare :c:func:`vmalloc()`. Essa allocherà memoria virtuale all'interno
dello spazio kernel. Questo è un blocco di memoria fisica non contiguo, ma
la MMU vi darà l'impressione che lo sia (quindi, sarà contiguo solo dal punto
di vista dei processori, non dal punto di vista dei driver dei dispositivi
esterni).
Se per qualche strana ragione avete davvero bisogno di una grossa quantità di
memoria fisica contigua, avete un problema: Linux non ha un buon supporto per
questo caso d'uso perché, dopo un po' di tempo, la frammentazione della memoria
rende l'operazione difficile. Il modo migliore per allocare un simile blocco
all'inizio dell'avvio del sistema è attraverso la procedura
:c:func:`alloc_bootmem()`.

Prima di inventare la vostra cache per gli oggetti più usati, considerate
l'uso di una cache slab disponibile in ``include/linux/slab.h``.

:c:macro:`current`
-------------------

Definita in ``include/asm/current.h``

Questa variabile globale (in realtà una macro) contiene un puntatore alla
struttura del processo corrente, quindi è valido solo dal contesto utente.
Per esempio, quando un processo esegue una chiamata di sistema, questo
punterà alla struttura dati del processo chiamate.
Nel contesto d'interruzione in suo valore **non è NULL**.

:c:func:`mdelay()`/:c:func:`udelay()`
-------------------------------------

Definite in ``include/asm/delay.h`` / ``include/linux/delay.h``

Le funzioni :c:func:`udelay()` e :c:func:`ndelay()` possono essere utilizzate
per brevi pause. Non usate grandi valori perché rischiate d'avere un
overflow - in questo contesto la funzione :c:func:`mdelay()` è utile,
oppure considerate :c:func:`msleep()`.

:c:func:`cpu_to_be32()`/:c:func:`be32_to_cpu()`/:c:func:`cpu_to_le32()`/:c:func:`le32_to_cpu()`
-----------------------------------------------------------------------------------------------

Definite in ``include/asm/byteorder.h``

La famiglia di funzioni :c:func:`cpu_to_be32()` (dove "32" può essere
sostituito da 64 o 16, e "be" con "le") forniscono un modo generico
per fare conversioni sull'ordine dei byte (endianess): esse ritornano
il valore convertito. Tutte le varianti supportano anche il processo inverso:
:c:func:`be32_to_cpu()`, eccetera.

Queste funzioni hanno principalmente due varianti: la variante per
puntatori, come :c:func:`cpu_to_be32p()`, che prende un puntatore
ad un tipo, e ritorna il valore convertito. L'altra variante per
la famiglia di conversioni "in-situ", come :c:func:`cpu_to_be32s()`,
che convertono il valore puntato da un puntatore, e ritornano void.

:c:func:`local_irq_save()`/:c:func:`local_irq_restore()`
--------------------------------------------------------

Definite in ``include/linux/irqflags.h``

Queste funzioni abilitano e disabilitano le interruzioni hardware
sul processore locale. Entrambe sono rientranti; esse salvano lo stato
precedente nel proprio argomento ``unsigned long flags``. Se sapete
che le interruzioni sono abilite, potete semplicemente utilizzare
:c:func:`local_irq_disable()` e :c:func:`local_irq_enable()`.

.. _it_local_bh_disable:

:c:func:`local_bh_disable()`/:c:func:`local_bh_enable()`
--------------------------------------------------------

Definite in ``include/linux/bottom_half.h``


Queste funzioni abilitano e disabilitano le interruzioni software
sul processore locale. Entrambe sono rientranti; se le interruzioni
software erano già state disabilitate in precedenza, rimarranno
disabilitate anche dopo aver invocato questa coppia di funzioni.
Lo scopo è di prevenire l'esecuzione di softirq e tasklet sul processore
attuale.

:c:func:`smp_processor_id()`
----------------------------

Definita in ``include/linux/smp.h``

:c:func:`get_cpu()` nega il diritto di prelazione (quindi non potete essere
spostati su un altro processore all'improvviso) e ritorna il numero
del processore attuale, fra 0 e ``NR_CPUS``. Da notare che non è detto
che la numerazione dei processori sia continua. Quando avete terminato,
ritornate allo stato precedente con :c:func:`put_cpu()`.

Se sapete che non dovete essere interrotti da altri processi (per esempio,
se siete in un contesto d'interruzione, o il diritto di prelazione
è disabilitato) potete utilizzare smp_processor_id().


``__init``/``__exit``/``__initdata``
------------------------------------

Definite in  ``include/linux/init.h``

Dopo l'avvio, il kernel libera una sezione speciale; le funzioni marcate
con ``__init`` e le strutture dati marcate con ``__initdata`` vengono
eliminate dopo il completamento dell'avvio: in modo simile i moduli eliminano
questa memoria dopo l'inizializzazione. ``__exit`` viene utilizzato per
dichiarare che una funzione verrà utilizzata solo in fase di rimozione:
la detta funzione verrà eliminata quando il file che la contiene non è
compilato come modulo. Guardate l'header file per informazioni. Da notare che
non ha senso avere una funzione marcata come ``__init`` e al tempo stesso
esportata ai moduli utilizzando :c:func:`EXPORT_SYMBOL()` o
:c:func:`EXPORT_SYMBOL_GPL()` - non funzionerà.


:c:func:`__initcall()`/:c:func:`module_init()`
----------------------------------------------

Definite in  ``include/linux/init.h`` / ``include/linux/module.h``

Molte parti del kernel funzionano bene come moduli (componenti del kernel
caricabili dinamicamente). L'utilizzo delle macro :c:func:`module_init()`
e :c:func:`module_exit()` semplifica la scrittura di codice che può funzionare
sia come modulo, sia come parte del kernel, senza l'ausilio di #ifdef.

La macro :c:func:`module_init()` definisce quale funzione dev'essere
chiamata quando il modulo viene inserito (se il file è stato compilato come
tale), o in fase di avvio : se il file non è stato compilato come modulo la
macro :c:func:`module_init()` diventa equivalente a :c:func:`__initcall()`,
la quale, tramite qualche magia del linker, s'assicura che la funzione venga
chiamata durante l'avvio.

La funzione può ritornare un numero d'errore negativo per scatenare un
fallimento del caricamento (sfortunatamente, questo non ha effetto se il
modulo è compilato come parte integrante del kernel). Questa funzione è chiamata
in contesto utente con le interruzioni abilitate, quindi potrebbe dormire.


:c:func:`module_exit()`
-----------------------


Definita in  ``include/linux/module.h``

Questa macro definisce la funzione che dev'essere chiamata al momento della
rimozione (o mai, nel caso in cui il file sia parte integrante del kernel).
Essa verrà chiamata solo quando il contatore d'uso del modulo raggiunge lo
zero. Questa funzione può anche dormire, ma non può fallire: tutto dev'essere
ripulito prima che la funzione ritorni.

Da notare che questa macro è opzionale: se non presente, il modulo non sarà
removibile (a meno che non usiate 'rmmod -f' ).


:c:func:`try_module_get()`/:c:func:`module_put()`
-------------------------------------------------

Definite in ``include/linux/module.h``

Queste funzioni maneggiano il contatore d'uso del modulo per proteggerlo dalla
rimozione (in aggiunta, un modulo non può essere rimosso se un altro modulo
utilizzo uno dei sui simboli esportati: vedere di seguito). Prima di eseguire
codice del modulo, dovreste chiamare :c:func:`try_module_get()` su quel modulo:
se fallisce significa che il modulo è stato rimosso e dovete agire come se
non fosse presente. Altrimenti, potete accedere al modulo in sicurezza, e
chiamare :c:func:`module_put()` quando avete finito.

La maggior parte delle strutture registrabili hanno un campo owner
(proprietario), come nella struttura
:c:type:`struct file_operations <file_operations>`.
Impostate questo campo al valore della macro ``THIS_MODULE``.


Code d'attesa ``include/linux/wait.h``
======================================

**[DORMONO]**

Una coda d'attesa è usata per aspettare che qualcuno vi attivi quando una
certa condizione s'avvera. Per evitare corse critiche, devono essere usate
con cautela. Dichiarate una :c:type:`wait_queue_head_t`, e poi i processi
che vogliono attendere il verificarsi di quella condizione dichiareranno
una :c:type:`wait_queue_entry_t` facendo riferimento a loro stessi, poi
metteranno questa in coda.

Dichiarazione
-------------

Potere dichiarare una ``wait_queue_head_t`` utilizzando la macro
:c:func:`DECLARE_WAIT_QUEUE_HEAD()` oppure utilizzando la procedura
:c:func:`init_waitqueue_head()` nel vostro codice d'inizializzazione.

Accodamento
-----------

Mettersi in una coda d'attesa è piuttosto complesso, perché dovete
mettervi in coda prima di verificare la condizione. Esiste una macro
a questo scopo: :c:func:`wait_event_interruptible()` (``include/linux/wait.h``).
Il primo argomento è la testa della coda d'attesa, e il secondo è
un'espressione che dev'essere valutata; la macro ritorna 0 quando questa
espressione è vera, altrimenti ``-ERESTARTSYS`` se è stato ricevuto un segnale.
La versione :c:func:`wait_event()` ignora i segnali.

Svegliare una procedura in coda
-------------------------------

Chiamate :c:func:`wake_up()` (``include/linux/wait.h``); questa attiverà tutti
i processi in coda. Ad eccezione se uno di questi è impostato come
``TASK_EXCLUSIVE``, in questo caso i rimanenti non verranno svegliati.
Nello stesso header file esistono altre varianti di questa funzione.

Operazioni atomiche
===================

Certe operazioni sono garantite come atomiche su tutte le piattaforme.
Il primo gruppo di operazioni utilizza :c:type:`atomic_t`
(``include/asm/atomic.h``); questo contiene un intero con segno (minimo 32bit),
e dovete utilizzare queste funzione per modificare o leggere variabili di tipo
:c:type:`atomic_t`. :c:func:`atomic_read()` e :c:func:`atomic_set()` leggono ed
impostano il contatore, :c:func:`atomic_add()`, :c:func:`atomic_sub()`,
:c:func:`atomic_inc()`, :c:func:`atomic_dec()`, e
:c:func:`atomic_dec_and_test()` (ritorna vero se raggiunge zero dopo essere
stata decrementata).

Sì. Ritorna vero (ovvero != 0) se la variabile atomica è zero.

Da notare che queste funzioni sono più lente rispetto alla normale aritmetica,
e quindi non dovrebbero essere usate a sproposito.

Il secondo gruppo di operazioni atomiche sono definite in
``include/linux/bitops.h`` ed agiscono sui bit d'una variabile di tipo
``unsigned long``. Queste operazioni prendono come argomento un puntatore
alla variabile, e un numero di bit dove 0 è quello meno significativo.
:c:func:`set_bit()`, :c:func:`clear_bit()` e :c:func:`change_bit()`
impostano, cancellano, ed invertono il bit indicato.
:c:func:`test_and_set_bit()`, :c:func:`test_and_clear_bit()` e
:c:func:`test_and_change_bit()` fanno la stessa cosa, ad eccezione che
ritornano vero se il bit era impostato; queste sono particolarmente
utili quando si vuole impostare atomicamente dei flag.

Con queste operazioni è possibile utilizzare indici di bit che eccedono
il valore ``BITS_PER_LONG``. Il comportamento è strano sulle piattaforme
big-endian quindi è meglio evitarlo.

Simboli
=======

All'interno del kernel, si seguono le normali regole del linker (ovvero,
a meno che un simbolo non venga dichiarato con visibilita limitata ad un
file con la parola chiave ``static``, esso può essere utilizzato in qualsiasi
parte del kernel). Nonostante ciò, per i moduli, esiste una tabella dei
simboli esportati che limita i punti di accesso al kernel. Anche i moduli
possono esportare simboli.

:c:func:`EXPORT_SYMBOL()`
-------------------------

Definita in ``include/linux/export.h``

Questo è il classico metodo per esportare un simbolo: i moduli caricati
dinamicamente potranno utilizzare normalmente il simbolo.

:c:func:`EXPORT_SYMBOL_GPL()`
-----------------------------

Definita in ``include/linux/export.h``

Essa è simile a :c:func:`EXPORT_SYMBOL()` ad eccezione del fatto che i
simboli esportati con :c:func:`EXPORT_SYMBOL_GPL()` possono essere
utilizzati solo dai moduli che hanno dichiarato una licenza compatibile
con la GPL attraverso :c:func:`MODULE_LICENSE()`. Questo implica che la
funzione esportata è considerata interna, e non una vera e propria interfaccia.
Alcuni manutentori e sviluppatori potrebbero comunque richiedere
:c:func:`EXPORT_SYMBOL_GPL()` quando si aggiungono nuove funzionalità o
interfacce.

:c:func:`EXPORT_SYMBOL_NS()`
----------------------------

Definita in ``include/linux/export.h``

Questa è una variate di `EXPORT_SYMBOL()` che permette di specificare uno
spazio dei nomi. Lo spazio dei nomi è documentato in
Documentation/translations/it_IT/core-api/symbol-namespaces.rst.

:c:func:`EXPORT_SYMBOL_NS_GPL()`
--------------------------------

Definita in ``include/linux/export.h``

Questa è una variate di `EXPORT_SYMBOL_GPL()` che permette di specificare uno
spazio dei nomi. Lo spazio dei nomi è documentato in
Documentation/translations/it_IT/core-api/symbol-namespaces.rst.

Procedure e convenzioni
=======================

Liste doppiamente concatenate ``include/linux/list.h``
------------------------------------------------------

Un tempo negli header del kernel c'erano tre gruppi di funzioni per
le liste concatenate, ma questa è stata la vincente. Se non avete particolari
necessità per una semplice lista concatenata, allora questa è una buona scelta.

In particolare, :c:func:`list_for_each_entry()` è utile.

Convenzione dei valori di ritorno
---------------------------------

Per codice chiamato in contesto utente, è molto comune sfidare le convenzioni
C e ritornare 0 in caso di successo, ed un codice di errore negativo
(eg. ``-EFAULT``) nei casi fallimentari. Questo potrebbe essere controintuitivo
a prima vista, ma è abbastanza diffuso nel kernel.

Utilizzate :c:func:`ERR_PTR()` (``include/linux/err.h``) per codificare
un numero d'errore negativo in un puntatore, e :c:func:`IS_ERR()` e
:c:func:`PTR_ERR()` per recuperarlo di nuovo: così si evita d'avere un
puntatore dedicato per il numero d'errore. Da brividi, ma in senso positivo.

Rompere la compilazione
-----------------------

Linus e gli altri sviluppatori a volte cambiano i nomi delle funzioni e
delle strutture nei kernel in sviluppo; questo non è solo per tenere
tutti sulle spine: questo riflette cambiamenti fondamentati (eg. la funzione
non può più essere chiamata con le funzioni attive, o fa controlli aggiuntivi,
o non fa più controlli che venivano fatti in precedenza). Solitamente a questo
s'accompagna un'adeguata e completa nota sulla lista di discussone
più adatta; cercate negli archivi. Solitamente eseguire una semplice
sostituzione su tutto un file rendere le cose **peggiori**.

Inizializzazione dei campi d'una struttura
------------------------------------------

Il metodo preferito per l'inizializzazione delle strutture è quello
di utilizzare gli inizializzatori designati, come definiti nello
standard ISO C99, eg::

    static struct block_device_operations opt_fops = {
            .open               = opt_open,
            .release            = opt_release,
            .ioctl              = opt_ioctl,
            .check_media_change = opt_media_change,
    };

Questo rende più facile la ricerca con grep, e rende più chiaro quale campo
viene impostato. Dovreste fare così perché si mostra meglio.

Estensioni GNU
--------------

Le estensioni GNU sono esplicitamente permesse nel kernel Linux. Da notare
che alcune delle più complesse non sono ben supportate, per via dello scarso
sviluppo, ma le seguenti sono da considerarsi la norma (per maggiori dettagli,
leggete la sezione "C Extensions" nella pagina info di GCC - Sì, davvero
la pagina info, la pagina man è solo un breve riassunto delle cose nella
pagina info).

-  Funzioni inline

-  Istruzioni in espressioni (ie. il costrutto ({ and }) ).

-  Dichiarate attributi di una funzione / variabile / tipo
   (__attribute__)

-  typeof

-  Array con lunghezza zero

-  Macro varargs

-  Aritmentica sui puntatori void

-  Inizializzatori non costanti

-  Istruzioni assembler (non al di fuori di 'arch/' e 'include/asm/')

-  Nomi delle funzioni come stringhe (__func__).

-  __builtin_constant_p()

Siate sospettosi quando utilizzate long long nel kernel, il codice generato
da gcc è orribile ed anche peggio: le divisioni e le moltiplicazioni non
funzionano sulle piattaforme i386 perché le rispettive funzioni di runtime
di GCC non sono incluse nell'ambiente del kernel.

C++
---

Solitamente utilizzare il C++ nel kernel è una cattiva idea perché
il kernel non fornisce il necessario ambiente di runtime e gli header file
non sono stati verificati. Rimane comunque possibile, ma non consigliato.
Se davvero volete usarlo, almeno evitate le eccezioni.

NUMif
-----

Viene generalmente considerato più pulito l'uso delle macro negli header file
(o all'inizio dei file .c) per astrarre funzioni piuttosto che utlizzare
l'istruzione di pre-processore \`#if' all'interno del codice sorgente.

Mettere le vostre cose nel kernel
=================================

Al fine d'avere le vostre cose in ordine per l'inclusione ufficiale, o
anche per avere patch pulite, c'è del lavoro amministrativo da fare:

-  Trovare chi è responsabile del codice che state modificando. Guardare in cima
   ai file sorgenti, all'interno del file ``MAINTAINERS``, ed alla fine
   di tutti nel file ``CREDITS``. Dovreste coordinarvi con queste persone
   per evitare di duplicare gli sforzi, o provare qualcosa che è già stato
   rigettato.

   Assicuratevi di mettere il vostro nome ed indirizzo email in cima a
   tutti i file che create o che maneggiate significativamente. Questo è
   il primo posto dove le persone guarderanno quando troveranno un baco,
   o quando **loro** vorranno fare una modifica.

-  Solitamente vorrete un'opzione di configurazione per la vostra modifica
   al kernel. Modificate ``Kconfig`` nella cartella giusta. Il linguaggio
   Config è facile con copia ed incolla, e c'è una completa documentazione
   nel file ``Documentation/kbuild/kconfig-language.rst``.

   Nella descrizione della vostra opzione, assicuratevi di parlare sia agli
   utenti esperti sia agli utente che non sanno nulla del vostro lavoro.
   Menzionate qui le incompatibilità ed i problemi. Chiaramente la
   descrizione deve terminare con “if in doubt, say N” (se siete in dubbio,
   dite N) (oppure, occasionalmente, \`Y'); questo è per le persone che non
   hanno idea di che cosa voi stiate parlando.

-  Modificate il file ``Makefile``: le variabili CONFIG sono esportate qui,
   quindi potete solitamente aggiungere una riga come la seguete
   "obj-$(CONFIG_xxx) += xxx.o". La sintassi è documentata nel file
   ``Documentation/kbuild/makefiles.rst``.

-  Aggiungete voi stessi in ``CREDITS`` se credete di aver fatto qualcosa di
   notevole, solitamente qualcosa che supera il singolo file (comunque il vostro
   nome dovrebbe essere all'inizio dei file sorgenti). ``MAINTAINERS`` significa
   che volete essere consultati quando vengono fatte delle modifiche ad un
   sottosistema, e quando ci sono dei bachi; questo implica molto di più di un
   semplice impegno su una parte del codice.

-  Infine, non dimenticatevi di leggere
   ``Documentation/process/submitting-patches.rst``.

Trucchetti del kernel
=====================

Dopo una rapida occhiata al codice, questi sono i preferiti. Sentitevi liberi
di aggiungerne altri.

``arch/x86/include/asm/delay.h``::

    #define ndelay(n) (__builtin_constant_p(n) ? \
            ((n) > 20000 ? __bad_ndelay() : __const_udelay((n) * 5ul)) : \
            __ndelay(n))


``include/linux/fs.h``::

    /*
     * Kernel pointers have redundant information, so we can use a
     * scheme where we can return either an error code or a dentry
     * pointer with the same return value.
     *
     * This should be a per-architecture thing, to allow different
     * error and pointer decisions.
     */
     #define ERR_PTR(err)    ((void *)((long)(err)))
     #define PTR_ERR(ptr)    ((long)(ptr))
     #define IS_ERR(ptr)     ((unsigned long)(ptr) > (unsigned long)(-1000))

``arch/x86/include/asm/uaccess_32.h:``::

    #define copy_to_user(to,from,n)                         \
            (__builtin_constant_p(n) ?                      \
             __constant_copy_to_user((to),(from),(n)) :     \
             __generic_copy_to_user((to),(from),(n)))


``arch/sparc/kernel/head.S:``::

    /*
     * Sun people can't spell worth damn. "compatability" indeed.
     * At least we *know* we can't spell, and use a spell-checker.
     */

    /* Uh, actually Linus it is I who cannot spell. Too much murky
     * Sparc assembly will do this to ya.
     */
    C_LABEL(cputypvar):
            .asciz "compatibility"

    /* Tested on SS-5, SS-10. Probably someone at Sun applied a spell-checker. */
            .align 4
    C_LABEL(cputypvar_sun4m):
            .asciz "compatible"


``arch/sparc/lib/checksum.S:``::

            /* Sun, you just can't beat me, you just can't.  Stop trying,
             * give up.  I'm serious, I am going to kick the living shit
             * out of you, game over, lights out.
             */


Ringraziamenti
==============

Ringrazio Andi Kleen per le sue idee, le risposte alle mie domande,
le correzioni dei miei errori, l'aggiunta di contenuti, eccetera.
Philipp Rumpf per l'ortografia e per aver reso più chiaro il testo, e
per alcuni eccellenti punti tutt'altro che ovvi. Werner Almesberger
per avermi fornito un ottimo riassunto di :c:func:`disable_irq()`,
e Jes Sorensen e Andrea Arcangeli per le precisazioni. Michael Elizabeth
Chastain per aver verificato ed aggiunto la sezione configurazione.
Telsa Gwynne per avermi insegnato DocBook.
