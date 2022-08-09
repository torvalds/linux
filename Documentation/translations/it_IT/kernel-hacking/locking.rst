.. include:: ../disclaimer-ita.rst

.. c:namespace:: it_IT

:Original: :ref:`Documentation/kernel-hacking/locking.rst <kernel_hacking_lock>`
:Translator: Federico Vaga <federico.vaga@vaga.pv.it>

.. _it_kernel_hacking_lock:

==========================================
L'inaffidabile guida alla sincronizzazione
==========================================

:Author: Rusty Russell

Introduzione
============

Benvenuto, alla notevole ed inaffidabile guida ai problemi di sincronizzazione
(locking) nel kernel. Questo documento descrive il sistema di sincronizzazione
nel kernel Linux 2.6.

Dato il largo utilizzo del multi-threading e della prelazione nel kernel
Linux, chiunque voglia dilettarsi col kernel deve conoscere i concetti
fondamentali della concorrenza e della sincronizzazione nei sistemi
multi-processore.

Il problema con la concorrenza
==============================

(Saltatelo se sapete già cos'è una corsa critica).

In un normale programma, potete incrementare un contatore nel seguente modo:

::

          contatore++;

Questo è quello che vi aspettereste che accada sempre:


.. table:: Risultati attesi

  +------------------------------------+------------------------------------+
  | Istanza 1                          | Istanza 2                          |
  +====================================+====================================+
  | leggi contatore (5)                |                                    |
  +------------------------------------+------------------------------------+
  | aggiungi 1 (6)                     |                                    |
  +------------------------------------+------------------------------------+
  | scrivi contatore (6)               |                                    |
  +------------------------------------+------------------------------------+
  |                                    | leggi contatore (6)                |
  +------------------------------------+------------------------------------+
  |                                    | aggiungi 1 (7)                     |
  +------------------------------------+------------------------------------+
  |                                    | scrivi contatore (7)               |
  +------------------------------------+------------------------------------+

Questo è quello che potrebbe succedere in realtà:

.. table:: Possibile risultato

  +------------------------------------+------------------------------------+
  | Istanza 1                          | Istanza 2                          |
  +====================================+====================================+
  | leggi contatore (5)                |                                    |
  +------------------------------------+------------------------------------+
  |                                    | leggi contatore (5)                |
  +------------------------------------+------------------------------------+
  | aggiungi 1 (6)                     |                                    |
  +------------------------------------+------------------------------------+
  |                                    | aggiungi 1 (6)                     |
  +------------------------------------+------------------------------------+
  | scrivi contatore (6)               |                                    |
  +------------------------------------+------------------------------------+
  |                                    | scrivi contatore (6)               |
  +------------------------------------+------------------------------------+


Corse critiche e sezioni critiche
---------------------------------

Questa sovrapposizione, ovvero quando un risultato dipende dal tempo che
intercorre fra processi diversi, è chiamata corsa critica. La porzione
di codice che contiene questo problema è chiamata sezione critica.
In particolar modo da quando Linux ha incominciato a girare su
macchine multi-processore, le sezioni critiche sono diventate uno dei
maggiori problemi di progettazione ed implementazione del kernel.

La prelazione può sortire gli stessi effetti, anche se c'è una sola CPU:
interrompendo un processo nella sua sezione critica otterremo comunque
la stessa corsa critica. In questo caso, il thread che si avvicenda
nell'esecuzione potrebbe eseguire anch'esso la sezione critica.

La soluzione è quella di riconoscere quando avvengono questi accessi
simultanei, ed utilizzare i *lock* per accertarsi che solo un'istanza
per volta possa entrare nella sezione critica. Il kernel offre delle buone
funzioni a questo scopo. E poi ci sono quelle meno buone, ma farò finta
che non esistano.

Sincronizzazione nel kernel Linux
=================================

Se posso darvi un suggerimento: non dormite mai con qualcuno più pazzo di
voi. Ma se dovessi darvi un suggerimento sulla sincronizzazione:
**mantenetela semplice**.

Siate riluttanti nell'introduzione di nuovi *lock*.

Abbastanza strano, quest'ultimo è l'esatto opposto del mio suggerimento
su quando **avete** dormito con qualcuno più pazzo di voi. E dovreste
pensare a prendervi un cane bello grande.

I due principali tipi di *lock* nel kernel: spinlock e mutex
------------------------------------------------------------

Ci sono due tipi principali di *lock* nel kernel. Il tipo fondamentale è lo
spinlock (``include/asm/spinlock.h``), un semplice *lock* che può essere
trattenuto solo da un processo: se non si può trattenere lo spinlock, allora
rimane in attesa attiva (in inglese *spinning*) finché non ci riesce.
Gli spinlock sono molto piccoli e rapidi, possono essere utilizzati ovunque.

Il secondo tipo è il mutex (``include/linux/mutex.h``): è come uno spinlock,
ma potreste bloccarvi trattenendolo. Se non potete trattenere un mutex
il vostro processo si auto-sospenderà; verrà riattivato quando il mutex
verrà rilasciato. Questo significa che il processore potrà occuparsi d'altro
mentre il vostro processo è in attesa. Esistono molti casi in cui non potete
permettervi di sospendere un processo (vedere
`Quali funzioni possono essere chiamate in modo sicuro dalle interruzioni?`_)
e quindi dovrete utilizzare gli spinlock.

Nessuno di questi *lock* è ricorsivo: vedere
`Stallo: semplice ed avanzato`_

I *lock* e i kernel per sistemi monoprocessore
----------------------------------------------

Per i kernel compilati senza ``CONFIG_SMP`` e senza ``CONFIG_PREEMPT``
gli spinlock non esistono. Questa è un'ottima scelta di progettazione:
quando nessun altro processo può essere eseguito in simultanea, allora
non c'è la necessità di avere un *lock*.

Se il kernel è compilato senza ``CONFIG_SMP`` ma con ``CONFIG_PREEMPT``,
allora gli spinlock disabilitano la prelazione; questo è sufficiente a
prevenire le corse critiche. Nella maggior parte dei casi, possiamo considerare
la prelazione equivalente ad un sistema multi-processore senza preoccuparci
di trattarla indipendentemente.

Dovreste verificare sempre la sincronizzazione con le opzioni ``CONFIG_SMP`` e
``CONFIG_PREEMPT`` abilitate, anche quando non avete un sistema
multi-processore, questo vi permetterà di identificare alcuni problemi
di sincronizzazione.

Come vedremo di seguito, i mutex continuano ad esistere perché sono necessari
per la sincronizzazione fra processi in contesto utente.

Sincronizzazione in contesto utente
-----------------------------------

Se avete una struttura dati che verrà utilizzata solo dal contesto utente,
allora, per proteggerla, potete utilizzare un semplice mutex
(``include/linux/mutex.h``). Questo è il caso più semplice: inizializzate il
mutex; invocate mutex_lock_interruptible() per trattenerlo e
mutex_unlock() per rilasciarlo. C'è anche mutex_lock()
ma questa dovrebbe essere evitata perché non ritorna in caso di segnali.

Per esempio: ``net/netfilter/nf_sockopt.c`` permette la registrazione
di nuove chiamate per setsockopt() e getsockopt()
usando la funzione nf_register_sockopt(). La registrazione e
la rimozione vengono eseguite solamente quando il modulo viene caricato
o scaricato (e durante l'avvio del sistema, qui non abbiamo concorrenza),
e la lista delle funzioni registrate viene consultata solamente quando
setsockopt() o getsockopt() sono sconosciute al sistema.
In questo caso ``nf_sockopt_mutex`` è perfetto allo scopo, in particolar modo
visto che setsockopt e getsockopt potrebbero dormire.

Sincronizzazione fra il contesto utente e i softirq
---------------------------------------------------

Se un softirq condivide dati col contesto utente, avete due problemi.
Primo, il contesto utente corrente potrebbe essere interroto da un softirq,
e secondo, la sezione critica potrebbe essere eseguita da un altro
processore. Questo è quando spin_lock_bh()
(``include/linux/spinlock.h``) viene utilizzato. Questo disabilita i softirq
sul processore e trattiene il *lock*. Invece, spin_unlock_bh() fa
l'opposto. (Il suffisso '_bh' è un residuo storico che fa riferimento al
"Bottom Halves", il vecchio nome delle interruzioni software. In un mondo
perfetto questa funzione si chiamerebbe 'spin_lock_softirq()').

Da notare che in questo caso potete utilizzare anche spin_lock_irq()
o spin_lock_irqsave(), queste fermano anche le interruzioni hardware:
vedere `Contesto di interruzione hardware`_.

Questo funziona alla perfezione anche sui sistemi monoprocessore: gli spinlock
svaniscono e questa macro diventa semplicemente local_bh_disable()
(``include/linux/interrupt.h``), la quale impedisce ai softirq d'essere
eseguiti.

Sincronizzazione fra contesto utente e i tasklet
------------------------------------------------

Questo caso è uguale al precedente, un tasklet viene eseguito da un softirq.

Sincronizzazione fra contesto utente e i timer
----------------------------------------------

Anche questo caso è uguale al precedente, un timer viene eseguito da un
softirq.
Dal punto di vista della sincronizzazione, tasklet e timer sono identici.

Sincronizzazione fra tasklet e timer
------------------------------------

Qualche volta un tasklet od un timer potrebbero condividere i dati con
un altro tasklet o timer

Lo stesso tasklet/timer
~~~~~~~~~~~~~~~~~~~~~~~

Dato che un tasklet non viene mai eseguito contemporaneamente su due
processori, non dovete preoccuparvi che sia rientrante (ovvero eseguito
più volte in contemporanea), perfino su sistemi multi-processore.

Differenti tasklet/timer
~~~~~~~~~~~~~~~~~~~~~~~~

Se un altro tasklet/timer vuole condividere dati col vostro tasklet o timer,
allora avrete bisogno entrambe di spin_lock() e
spin_unlock(). Qui spin_lock_bh() è inutile, siete già
in un tasklet ed avete la garanzia che nessun altro verrà eseguito sullo
stesso processore.

Sincronizzazione fra softirq
----------------------------

Spesso un softirq potrebbe condividere dati con se stesso o un tasklet/timer.

Lo stesso softirq
~~~~~~~~~~~~~~~~~

Lo stesso softirq può essere eseguito su un diverso processore: allo scopo
di migliorare le prestazioni potete utilizzare dati riservati ad ogni
processore (vedere `Dati per processore`_). Se siete arrivati
fino a questo punto nell'uso dei softirq, probabilmente tenete alla scalabilità
delle prestazioni abbastanza da giustificarne la complessità aggiuntiva.

Dovete utilizzare spin_lock() e spin_unlock() per
proteggere i dati condivisi.

Diversi Softirqs
~~~~~~~~~~~~~~~~

Dovete utilizzare spin_lock() e spin_unlock() per
proteggere i dati condivisi, che siano timer, tasklet, diversi softirq o
lo stesso o altri softirq: uno qualsiasi di essi potrebbe essere in esecuzione
su un diverso processore.

.. _`it_hardirq-context`:

Contesto di interruzione hardware
=================================

Solitamente le interruzioni hardware comunicano con un tasklet o un softirq.
Spesso questo si traduce nel mettere in coda qualcosa da fare che verrà
preso in carico da un softirq.

Sincronizzazione fra interruzioni hardware e softirq/tasklet
------------------------------------------------------------

Se un gestore di interruzioni hardware condivide dati con un softirq, allora
avrete due preoccupazioni. Primo, il softirq può essere interrotto da
un'interruzione hardware, e secondo, la sezione critica potrebbe essere
eseguita da un'interruzione hardware su un processore diverso. Questo è il caso
dove spin_lock_irq() viene utilizzato. Disabilita le interruzioni
sul processore che l'esegue, poi trattiene il lock. spin_unlock_irq()
fa l'opposto.

Il gestore d'interruzione hardware non ha bisogno di usare spin_lock_irq()
perché i softirq non possono essere eseguiti quando il gestore d'interruzione
hardware è in esecuzione: per questo si può usare spin_lock(), che è un po'
più veloce. L'unica eccezione è quando un altro gestore d'interruzioni
hardware utilizza lo stesso *lock*: spin_lock_irq() impedirà a questo
secondo gestore di interrompere quello in esecuzione.

Questo funziona alla perfezione anche sui sistemi monoprocessore: gli spinlock
svaniscono e questa macro diventa semplicemente local_irq_disable()
(``include/asm/smp.h``), la quale impedisce a softirq/tasklet/BH d'essere
eseguiti.

spin_lock_irqsave() (``include/linux/spinlock.h``) è una variante che
salva lo stato delle interruzioni in una variabile, questa verrà poi passata
a spin_unlock_irqrestore(). Questo significa che lo stesso codice
potrà essere utilizzato in un'interruzione hardware (dove le interruzioni sono
già disabilitate) e in un softirq (dove la disabilitazione delle interruzioni
è richiesta).

Da notare che i softirq (e quindi tasklet e timer) sono eseguiti al ritorno
da un'interruzione hardware, quindi spin_lock_irq() interrompe
anche questi. Tenuto conto di questo si può dire che
spin_lock_irqsave() è la funzione di sincronizzazione più generica
e potente.

Sincronizzazione fra due gestori d'interruzioni hardware
--------------------------------------------------------

Condividere dati fra due gestori di interruzione hardware è molto raro, ma se
succede, dovreste usare spin_lock_irqsave(): è una specificità
dell'architettura il fatto che tutte le interruzioni vengano interrotte
quando si eseguono di gestori di interruzioni.

Bigino della sincronizzazione
=============================

Pete Zaitcev ci offre il seguente riassunto:

-  Se siete in un contesto utente (una qualsiasi chiamata di sistema)
   e volete sincronizzarvi con altri processi, usate i mutex. Potete trattenere
   il mutex e dormire (``copy_from_user*(`` o ``kmalloc(x,GFP_KERNEL)``).

-  Altrimenti (== i dati possono essere manipolati da un'interruzione) usate
   spin_lock_irqsave() e spin_unlock_irqrestore().

-  Evitate di trattenere uno spinlock per più di 5 righe di codice incluse
   le chiamate a funzione (ad eccezione di quell per l'accesso come
   readb()).

Tabella dei requisiti minimi
----------------------------

La tabella seguente illustra i requisiti **minimi** per la sincronizzazione fra
diversi contesti. In alcuni casi, lo stesso contesto può essere eseguito solo
da un processore per volta, quindi non ci sono requisiti per la
sincronizzazione (per esempio, un thread può essere eseguito solo su un
processore alla volta, ma se deve condividere dati con un altro thread, allora
la sincronizzazione è necessaria).

Ricordatevi il suggerimento qui sopra: potete sempre usare
spin_lock_irqsave(), che è un sovrainsieme di tutte le altre funzioni
per spinlock.

============== ============= ============= ========= ========= ========= ========= ======= ======= ============== ==============
.              IRQ Handler A IRQ Handler B Softirq A Softirq B Tasklet A Tasklet B Timer A Timer B User Context A User Context B
============== ============= ============= ========= ========= ========= ========= ======= ======= ============== ==============
IRQ Handler A  None
IRQ Handler B  SLIS          None
Softirq A      SLI           SLI           SL
Softirq B      SLI           SLI           SL        SL
Tasklet A      SLI           SLI           SL        SL        None
Tasklet B      SLI           SLI           SL        SL        SL        None
Timer A        SLI           SLI           SL        SL        SL        SL        None
Timer B        SLI           SLI           SL        SL        SL        SL        SL      None
User Context A SLI           SLI           SLBH      SLBH      SLBH      SLBH      SLBH    SLBH    None
User Context B SLI           SLI           SLBH      SLBH      SLBH      SLBH      SLBH    SLBH    MLI            None
============== ============= ============= ========= ========= ========= ========= ======= ======= ============== ==============

Table: Tabella dei requisiti per la sincronizzazione

+--------+----------------------------+
| SLIS   | spin_lock_irqsave          |
+--------+----------------------------+
| SLI    | spin_lock_irq              |
+--------+----------------------------+
| SL     | spin_lock                  |
+--------+----------------------------+
| SLBH   | spin_lock_bh               |
+--------+----------------------------+
| MLI    | mutex_lock_interruptible   |
+--------+----------------------------+

Table: Legenda per la tabella dei requisiti per la sincronizzazione

Le funzioni *trylock*
=====================

Ci sono funzioni che provano a trattenere un *lock* solo una volta e
ritornano immediatamente comunicato il successo od il fallimento
dell'operazione. Posso essere usate quando non serve accedere ai dati
protetti dal *lock* quando qualche altro thread lo sta già facendo
trattenendo il *lock*. Potrete acquisire il *lock* più tardi se vi
serve accedere ai dati protetti da questo *lock*.

La funzione spin_trylock() non ritenta di acquisire il *lock*,
se ci riesce al primo colpo ritorna un valore diverso da zero, altrimenti
se fallisce ritorna 0. Questa funzione può essere utilizzata in un qualunque
contesto, ma come spin_lock(): dovete disabilitare i contesti che
potrebbero interrompervi e quindi trattenere lo spinlock.

La funzione mutex_trylock() invece di sospendere il vostro processo
ritorna un valore diverso da zero se è possibile trattenere il lock al primo
colpo, altrimenti se fallisce ritorna 0. Nonostante non dorma, questa funzione
non può essere usata in modo sicuro in contesti di interruzione hardware o
software.

Esempi più comuni
=================

Guardiamo un semplice esempio: una memoria che associa nomi a numeri.
La memoria tiene traccia di quanto spesso viene utilizzato ogni oggetto;
quando è piena, l'oggetto meno usato viene eliminato.

Tutto in contesto utente
------------------------

Nel primo esempio, supponiamo che tutte le operazioni avvengano in contesto
utente (in soldoni, da una chiamata di sistema), quindi possiamo dormire.
Questo significa che possiamo usare i mutex per proteggere la nostra memoria
e tutti gli oggetti che contiene. Ecco il codice::

    #include <linux/list.h>
    #include <linux/slab.h>
    #include <linux/string.h>
    #include <linux/mutex.h>
    #include <asm/errno.h>

    struct object
    {
            struct list_head list;
            int id;
            char name[32];
            int popularity;
    };

    /* Protects the cache, cache_num, and the objects within it */
    static DEFINE_MUTEX(cache_lock);
    static LIST_HEAD(cache);
    static unsigned int cache_num = 0;
    #define MAX_CACHE_SIZE 10

    /* Must be holding cache_lock */
    static struct object *__cache_find(int id)
    {
            struct object *i;

            list_for_each_entry(i, &cache, list)
                    if (i->id == id) {
                            i->popularity++;
                            return i;
                    }
            return NULL;
    }

    /* Must be holding cache_lock */
    static void __cache_delete(struct object *obj)
    {
            BUG_ON(!obj);
            list_del(&obj->list);
            kfree(obj);
            cache_num--;
    }

    /* Must be holding cache_lock */
    static void __cache_add(struct object *obj)
    {
            list_add(&obj->list, &cache);
            if (++cache_num > MAX_CACHE_SIZE) {
                    struct object *i, *outcast = NULL;
                    list_for_each_entry(i, &cache, list) {
                            if (!outcast || i->popularity < outcast->popularity)
                                    outcast = i;
                    }
                    __cache_delete(outcast);
            }
    }

    int cache_add(int id, const char *name)
    {
            struct object *obj;

            if ((obj = kmalloc(sizeof(*obj), GFP_KERNEL)) == NULL)
                    return -ENOMEM;

            strscpy(obj->name, name, sizeof(obj->name));
            obj->id = id;
            obj->popularity = 0;

            mutex_lock(&cache_lock);
            __cache_add(obj);
            mutex_unlock(&cache_lock);
            return 0;
    }

    void cache_delete(int id)
    {
            mutex_lock(&cache_lock);
            __cache_delete(__cache_find(id));
            mutex_unlock(&cache_lock);
    }

    int cache_find(int id, char *name)
    {
            struct object *obj;
            int ret = -ENOENT;

            mutex_lock(&cache_lock);
            obj = __cache_find(id);
            if (obj) {
                    ret = 0;
                    strcpy(name, obj->name);
            }
            mutex_unlock(&cache_lock);
            return ret;
    }

Da notare che ci assicuriamo sempre di trattenere cache_lock quando
aggiungiamo, rimuoviamo od ispezioniamo la memoria: sia la struttura
della memoria che il suo contenuto sono protetti dal *lock*. Questo
caso è semplice dato che copiamo i dati dall'utente e non permettiamo
mai loro di accedere direttamente agli oggetti.

C'è una piccola ottimizzazione qui: nella funzione cache_add()
impostiamo i campi dell'oggetto prima di acquisire il *lock*. Questo è
sicuro perché nessun altro potrà accedervi finché non lo inseriremo
nella memoria.

Accesso dal contesto utente
---------------------------

Ora consideriamo il caso in cui cache_find() può essere invocata
dal contesto d'interruzione: sia hardware che software. Un esempio potrebbe
essere un timer che elimina oggetti dalla memoria.

Qui di seguito troverete la modifica nel formato *patch*: le righe ``-``
sono quelle rimosse, mentre quelle ``+`` sono quelle aggiunte.

::

    --- cache.c.usercontext 2003-12-09 13:58:54.000000000 +1100
    +++ cache.c.interrupt   2003-12-09 14:07:49.000000000 +1100
    @@ -12,7 +12,7 @@
             int popularity;
     };

    -static DEFINE_MUTEX(cache_lock);
    +static DEFINE_SPINLOCK(cache_lock);
     static LIST_HEAD(cache);
     static unsigned int cache_num = 0;
     #define MAX_CACHE_SIZE 10
    @@ -55,6 +55,7 @@
     int cache_add(int id, const char *name)
     {
             struct object *obj;
    +        unsigned long flags;

             if ((obj = kmalloc(sizeof(*obj), GFP_KERNEL)) == NULL)
                     return -ENOMEM;
    @@ -63,30 +64,33 @@
             obj->id = id;
             obj->popularity = 0;

    -        mutex_lock(&cache_lock);
    +        spin_lock_irqsave(&cache_lock, flags);
             __cache_add(obj);
    -        mutex_unlock(&cache_lock);
    +        spin_unlock_irqrestore(&cache_lock, flags);
             return 0;
     }

     void cache_delete(int id)
     {
    -        mutex_lock(&cache_lock);
    +        unsigned long flags;
    +
    +        spin_lock_irqsave(&cache_lock, flags);
             __cache_delete(__cache_find(id));
    -        mutex_unlock(&cache_lock);
    +        spin_unlock_irqrestore(&cache_lock, flags);
     }

     int cache_find(int id, char *name)
     {
             struct object *obj;
             int ret = -ENOENT;
    +        unsigned long flags;

    -        mutex_lock(&cache_lock);
    +        spin_lock_irqsave(&cache_lock, flags);
             obj = __cache_find(id);
             if (obj) {
                     ret = 0;
                     strcpy(name, obj->name);
             }
    -        mutex_unlock(&cache_lock);
    +        spin_unlock_irqrestore(&cache_lock, flags);
             return ret;
     }

Da notare che spin_lock_irqsave() disabiliterà le interruzioni
se erano attive, altrimenti non farà niente (quando siamo già in un contesto
d'interruzione); dunque queste funzioni possono essere chiamante in
sicurezza da qualsiasi contesto.

Sfortunatamente, cache_add() invoca kmalloc() con
l'opzione ``GFP_KERNEL`` che è permessa solo in contesto utente. Ho supposto
che cache_add() venga chiamata dal contesto utente, altrimenti
questa opzione deve diventare un parametro di cache_add().

Esporre gli oggetti al di fuori del file
----------------------------------------

Se i vostri oggetti contengono più informazioni, potrebbe non essere
sufficiente copiare i dati avanti e indietro: per esempio, altre parti del
codice potrebbero avere un puntatore a questi oggetti piuttosto che cercarli
ogni volta. Questo introduce due problemi.

Il primo problema è che utilizziamo ``cache_lock`` per proteggere gli oggetti:
dobbiamo renderlo dinamico così che il resto del codice possa usarlo. Questo
rende la sincronizzazione più complicata dato che non avviene più in un unico
posto.

Il secondo problema è il problema del ciclo di vita: se un'altra struttura
mantiene un puntatore ad un oggetto, presumibilmente si aspetta che questo
puntatore rimanga valido. Sfortunatamente, questo è garantito solo mentre
si trattiene il *lock*, altrimenti qualcuno potrebbe chiamare
cache_delete() o peggio, aggiungere un oggetto che riutilizza lo
stesso indirizzo.

Dato che c'è un solo *lock*, non potete trattenerlo a vita: altrimenti
nessun altro potrà eseguire il proprio lavoro.

La soluzione a questo problema è l'uso di un contatore di riferimenti:
chiunque punti ad un oggetto deve incrementare il contatore, e decrementarlo
quando il puntatore non viene più usato. Quando il contatore raggiunge lo zero
significa che non è più usato e l'oggetto può essere rimosso.

Ecco il codice::

    --- cache.c.interrupt   2003-12-09 14:25:43.000000000 +1100
    +++ cache.c.refcnt  2003-12-09 14:33:05.000000000 +1100
    @@ -7,6 +7,7 @@
     struct object
     {
             struct list_head list;
    +        unsigned int refcnt;
             int id;
             char name[32];
             int popularity;
    @@ -17,6 +18,35 @@
     static unsigned int cache_num = 0;
     #define MAX_CACHE_SIZE 10

    +static void __object_put(struct object *obj)
    +{
    +        if (--obj->refcnt == 0)
    +                kfree(obj);
    +}
    +
    +static void __object_get(struct object *obj)
    +{
    +        obj->refcnt++;
    +}
    +
    +void object_put(struct object *obj)
    +{
    +        unsigned long flags;
    +
    +        spin_lock_irqsave(&cache_lock, flags);
    +        __object_put(obj);
    +        spin_unlock_irqrestore(&cache_lock, flags);
    +}
    +
    +void object_get(struct object *obj)
    +{
    +        unsigned long flags;
    +
    +        spin_lock_irqsave(&cache_lock, flags);
    +        __object_get(obj);
    +        spin_unlock_irqrestore(&cache_lock, flags);
    +}
    +
     /* Must be holding cache_lock */
     static struct object *__cache_find(int id)
     {
    @@ -35,6 +65,7 @@
     {
             BUG_ON(!obj);
             list_del(&obj->list);
    +        __object_put(obj);
             cache_num--;
     }

    @@ -63,6 +94,7 @@
             strscpy(obj->name, name, sizeof(obj->name));
             obj->id = id;
             obj->popularity = 0;
    +        obj->refcnt = 1; /* The cache holds a reference */

             spin_lock_irqsave(&cache_lock, flags);
             __cache_add(obj);
    @@ -79,18 +111,15 @@
             spin_unlock_irqrestore(&cache_lock, flags);
     }

    -int cache_find(int id, char *name)
    +struct object *cache_find(int id)
     {
             struct object *obj;
    -        int ret = -ENOENT;
             unsigned long flags;

             spin_lock_irqsave(&cache_lock, flags);
             obj = __cache_find(id);
    -        if (obj) {
    -                ret = 0;
    -                strcpy(name, obj->name);
    -        }
    +        if (obj)
    +                __object_get(obj);
             spin_unlock_irqrestore(&cache_lock, flags);
    -        return ret;
    +        return obj;
     }

Abbiamo incapsulato il contatore di riferimenti nelle tipiche funzioni
di 'get' e 'put'. Ora possiamo ritornare l'oggetto da cache_find()
col vantaggio che l'utente può dormire trattenendo l'oggetto (per esempio,
copy_to_user() per copiare il nome verso lo spazio utente).

Un altro punto da notare è che ho detto che il contatore dovrebbe incrementarsi
per ogni puntatore ad un oggetto: quindi il contatore di riferimenti è 1
quando l'oggetto viene inserito nella memoria. In altre versione il framework
non trattiene un riferimento per se, ma diventa più complicato.

Usare operazioni atomiche per il contatore di riferimenti
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

In sostanza, :c:type:`atomic_t` viene usato come contatore di riferimenti.
Ci sono un certo numbero di operazioni atomiche definite
in ``include/asm/atomic.h``: queste sono garantite come atomiche su qualsiasi
processore del sistema, quindi non sono necessari i *lock*. In questo caso è
più semplice rispetto all'uso degli spinlock, benché l'uso degli spinlock
sia più elegante per casi non banali. Le funzioni atomic_inc() e
atomic_dec_and_test() vengono usate al posto dei tipici operatori di
incremento e decremento, e i *lock* non sono più necessari per proteggere il
contatore stesso.

::

    --- cache.c.refcnt  2003-12-09 15:00:35.000000000 +1100
    +++ cache.c.refcnt-atomic   2003-12-11 15:49:42.000000000 +1100
    @@ -7,7 +7,7 @@
     struct object
     {
             struct list_head list;
    -        unsigned int refcnt;
    +        atomic_t refcnt;
             int id;
             char name[32];
             int popularity;
    @@ -18,33 +18,15 @@
     static unsigned int cache_num = 0;
     #define MAX_CACHE_SIZE 10

    -static void __object_put(struct object *obj)
    -{
    -        if (--obj->refcnt == 0)
    -                kfree(obj);
    -}
    -
    -static void __object_get(struct object *obj)
    -{
    -        obj->refcnt++;
    -}
    -
     void object_put(struct object *obj)
     {
    -        unsigned long flags;
    -
    -        spin_lock_irqsave(&cache_lock, flags);
    -        __object_put(obj);
    -        spin_unlock_irqrestore(&cache_lock, flags);
    +        if (atomic_dec_and_test(&obj->refcnt))
    +                kfree(obj);
     }

     void object_get(struct object *obj)
     {
    -        unsigned long flags;
    -
    -        spin_lock_irqsave(&cache_lock, flags);
    -        __object_get(obj);
    -        spin_unlock_irqrestore(&cache_lock, flags);
    +        atomic_inc(&obj->refcnt);
     }

     /* Must be holding cache_lock */
    @@ -65,7 +47,7 @@
     {
             BUG_ON(!obj);
             list_del(&obj->list);
    -        __object_put(obj);
    +        object_put(obj);
             cache_num--;
     }

    @@ -94,7 +76,7 @@
             strscpy(obj->name, name, sizeof(obj->name));
             obj->id = id;
             obj->popularity = 0;
    -        obj->refcnt = 1; /* The cache holds a reference */
    +        atomic_set(&obj->refcnt, 1); /* The cache holds a reference */

             spin_lock_irqsave(&cache_lock, flags);
             __cache_add(obj);
    @@ -119,7 +101,7 @@
             spin_lock_irqsave(&cache_lock, flags);
             obj = __cache_find(id);
             if (obj)
    -                __object_get(obj);
    +                object_get(obj);
             spin_unlock_irqrestore(&cache_lock, flags);
             return obj;
     }

Proteggere l'oggetto stesso
---------------------------

In questo esempio, assumiamo che gli oggetti (ad eccezione del contatore
di riferimenti) non cambino mai dopo la loro creazione. Se vogliamo permettere
al nome di cambiare abbiamo tre possibilità:

-  Si può togliere static da ``cache_lock`` e dire agli utenti che devono
   trattenere il *lock* prima di modificare il nome di un oggetto.

-  Si può fornire una funzione cache_obj_rename() che prende il
   *lock* e cambia il nome per conto del chiamante; si dirà poi agli utenti
   di usare questa funzione.

-  Si può decidere che ``cache_lock`` protegge solo la memoria stessa, ed
   un altro *lock* è necessario per la protezione del nome.

Teoricamente, possiamo avere un *lock* per ogni campo e per ogni oggetto.
In pratica, le varianti più comuni sono:

-  un *lock* che protegge l'infrastruttura (la lista ``cache`` di questo
   esempio) e gli oggetti. Questo è quello che abbiamo fatto finora.

-  un *lock* che protegge l'infrastruttura (inclusi i puntatori alla lista
   negli oggetti), e un *lock* nell'oggetto per proteggere il resto
   dell'oggetto stesso.

-  *lock* multipli per proteggere l'infrastruttura (per esempio un *lock*
   per ogni lista), possibilmente con un *lock* per oggetto.

Qui di seguito un'implementazione con "un lock per oggetto":

::

    --- cache.c.refcnt-atomic   2003-12-11 15:50:54.000000000 +1100
    +++ cache.c.perobjectlock   2003-12-11 17:15:03.000000000 +1100
    @@ -6,11 +6,17 @@

     struct object
     {
    +        /* These two protected by cache_lock. */
             struct list_head list;
    +        int popularity;
    +
             atomic_t refcnt;
    +
    +        /* Doesn't change once created. */
             int id;
    +
    +        spinlock_t lock; /* Protects the name */
             char name[32];
    -        int popularity;
     };

     static DEFINE_SPINLOCK(cache_lock);
    @@ -77,6 +84,7 @@
             obj->id = id;
             obj->popularity = 0;
             atomic_set(&obj->refcnt, 1); /* The cache holds a reference */
    +        spin_lock_init(&obj->lock);

             spin_lock_irqsave(&cache_lock, flags);
             __cache_add(obj);

Da notare che ho deciso che il contatore di popolarità dovesse essere
protetto da ``cache_lock`` piuttosto che dal *lock* dell'oggetto; questo
perché è logicamente parte dell'infrastruttura (come
:c:type:`struct list_head <list_head>` nell'oggetto). In questo modo,
in __cache_add(), non ho bisogno di trattenere il *lock* di ogni
oggetto mentre si cerca il meno popolare.

Ho anche deciso che il campo id è immutabile, quindi non ho bisogno di
trattenere il lock dell'oggetto quando si usa __cache_find()
per leggere questo campo; il *lock* dell'oggetto è usato solo dal chiamante
che vuole leggere o scrivere il campo name.

Inoltre, da notare che ho aggiunto un commento che descrive i dati che sono
protetti dal *lock*. Questo è estremamente importante in quanto descrive il
comportamento del codice, che altrimenti sarebbe di difficile comprensione
leggendo solamente il codice. E come dice Alan Cox: “Lock data, not code”.

Problemi comuni
===============

Stallo: semplice ed avanzato
----------------------------

Esiste un tipo di  baco dove un pezzo di codice tenta di trattenere uno
spinlock due volte: questo rimarrà in attesa attiva per sempre aspettando che
il *lock* venga rilasciato (in Linux spinlocks, rwlocks e mutex non sono
ricorsivi).
Questo è facile da diagnosticare: non è uno di quei problemi che ti tengono
sveglio 5 notti a parlare da solo.

Un caso un pochino più complesso; immaginate d'avere una spazio condiviso
fra un softirq ed il contesto utente. Se usate spin_lock() per
proteggerlo, il contesto utente potrebbe essere interrotto da un softirq
mentre trattiene il lock, da qui il softirq rimarrà in attesa attiva provando
ad acquisire il *lock* già trattenuto nel contesto utente.

Questi casi sono chiamati stalli (*deadlock*), e come mostrato qui sopra,
può succedere anche con un solo processore (Ma non sui sistemi
monoprocessore perché gli spinlock spariscano quando il kernel è compilato
con ``CONFIG_SMP``\ =n. Nonostante ciò, nel secondo caso avrete comunque
una corruzione dei dati).

Questi casi sono facili da diagnosticare; sui sistemi multi-processore
il supervisione (*watchdog*) o l'opzione di compilazione ``DEBUG_SPINLOCK``
(``include/linux/spinlock.h``) permettono di scovare immediatamente quando
succedono.

Esiste un caso più complesso che è conosciuto come l'abbraccio della morte;
questo coinvolge due o più *lock*. Diciamo che avete un vettore di hash in cui
ogni elemento è uno spinlock a cui è associata una lista di elementi con lo
stesso hash. In un gestore di interruzioni software, dovete modificare un
oggetto e spostarlo su un altro hash; quindi dovrete trattenete lo spinlock
del vecchio hash e di quello nuovo, quindi rimuovere l'oggetto dal vecchio ed
inserirlo nel nuovo.

Qui abbiamo due problemi. Primo, se il vostro codice prova a spostare un
oggetto all'interno della stessa lista, otterrete uno stallo visto che
tenterà di trattenere lo stesso *lock* due volte. Secondo, se la stessa
interruzione software su un altro processore sta tentando di spostare
un altro oggetto nella direzione opposta, potrebbe accadere quanto segue:

+---------------------------------+---------------------------------+
| CPU 1                           | CPU 2                           |
+=================================+=================================+
| Trattiene *lock* A -> OK        | Trattiene *lock* B -> OK        |
+---------------------------------+---------------------------------+
| Trattiene *lock* B -> attesa    | Trattiene *lock* A -> attesa    |
+---------------------------------+---------------------------------+

Table: Conseguenze

Entrambe i processori rimarranno in attesa attiva sul *lock* per sempre,
aspettando che l'altro lo rilasci. Sembra e puzza come un blocco totale.

Prevenire gli stalli
--------------------

I libri di testo vi diranno che se trattenete i *lock* sempre nello stesso
ordine non avrete mai un simile stallo. La pratica vi dirà che questo
approccio non funziona all'ingrandirsi del sistema: quando creo un nuovo
*lock* non ne capisco abbastanza del kernel per dire in quale dei 5000 *lock*
si incastrerà.

I *lock* migliori sono quelli incapsulati: non vengono esposti nei file di
intestazione, e non vengono mai trattenuti fuori dallo stesso file. Potete
rileggere questo codice e vedere che non ci sarà mai uno stallo perché
non tenterà mai di trattenere un altro *lock* quando lo ha già.
Le persone che usano il vostro codice non devono nemmeno sapere che voi
state usando dei *lock*.

Un classico problema deriva dall'uso di *callback* e di *hook*: se li
chiamate mentre trattenete un *lock*, rischiate uno stallo o un abbraccio
della morte (chi lo sa cosa farà una *callback*?).

Ossessiva prevenzione degli stalli
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Gli stalli sono un problema, ma non così terribile come la corruzione dei dati.
Un pezzo di codice trattiene un *lock* di lettura, cerca in una lista,
fallisce nel trovare quello che vuole, quindi rilascia il *lock* di lettura,
trattiene un *lock* di scrittura ed inserisce un oggetto; questo genere di
codice presenta una corsa critica.

Se non riuscite a capire il perché, per favore state alla larga dal mio
codice.

corsa fra temporizzatori: un passatempo del kernel
--------------------------------------------------

I temporizzatori potrebbero avere dei problemi con le corse critiche.
Considerate una collezione di oggetti (liste, hash, eccetera) dove ogni oggetto
ha un temporizzatore che sta per distruggerlo.

Se volete eliminare l'intera collezione (diciamo quando rimuovete un modulo),
potreste fare come segue::

            /* THIS CODE BAD BAD BAD BAD: IF IT WAS ANY WORSE IT WOULD USE
               HUNGARIAN NOTATION */
            spin_lock_bh(&list_lock);

            while (list) {
                    struct foo *next = list->next;
                    del_timer(&list->timer);
                    kfree(list);
                    list = next;
            }

            spin_unlock_bh(&list_lock);

Primo o poi, questo esploderà su un sistema multiprocessore perché un
temporizzatore potrebbe essere già partiro prima di spin_lock_bh(),
e prenderà il *lock* solo dopo spin_unlock_bh(), e cercherà
di eliminare il suo oggetto (che però è già stato eliminato).

Questo può essere evitato controllando il valore di ritorno di
del_timer(): se ritorna 1, il temporizzatore è stato già
rimosso. Se 0, significa (in questo caso) che il temporizzatore è in
esecuzione, quindi possiamo fare come segue::

            retry:
                    spin_lock_bh(&list_lock);

                    while (list) {
                            struct foo *next = list->next;
                            if (!del_timer(&list->timer)) {
                                    /* Give timer a chance to delete this */
                                    spin_unlock_bh(&list_lock);
                                    goto retry;
                            }
                            kfree(list);
                            list = next;
                    }

                    spin_unlock_bh(&list_lock);

Un altro problema è l'eliminazione dei temporizzatori che si riavviano
da soli (chiamando add_timer() alla fine della loro esecuzione).
Dato che questo è un problema abbastanza comune con una propensione
alle corse critiche, dovreste usare del_timer_sync()
(``include/linux/timer.h``) per gestire questo caso. Questa ritorna il
numero di volte che il temporizzatore è stato interrotto prima che
fosse in grado di fermarlo senza che si riavviasse.

Velocità della sincronizzazione
===============================

Ci sono tre cose importanti da tenere in considerazione quando si valuta
la velocità d'esecuzione di un pezzo di codice che necessita di
sincronizzazione. La prima è la concorrenza: quante cose rimangono in attesa
mentre qualcuno trattiene un *lock*. La seconda è il tempo necessario per
acquisire (senza contese) e rilasciare un *lock*. La terza è di usare meno
*lock* o di più furbi. Immagino che i *lock* vengano usati regolarmente,
altrimenti, non sareste interessati all'efficienza.

La concorrenza dipende da quanto a lungo un *lock* è trattenuto: dovreste
trattenere un *lock* solo il tempo minimo necessario ma non un istante in più.
Nella memoria dell'esempio precedente, creiamo gli oggetti senza trattenere
il *lock*, poi acquisiamo il *lock* quando siamo pronti per inserirlo nella
lista.

Il tempo di acquisizione di un *lock* dipende da quanto danno fa
l'operazione sulla *pipeline* (ovvero stalli della *pipeline*) e quant'è
probabile che il processore corrente sia stato anche l'ultimo ad acquisire
il *lock* (in pratica, il *lock* è nella memoria cache del processore
corrente?): su sistemi multi-processore questa probabilità precipita
rapidamente. Consideriamo un processore Intel Pentium III a 700Mhz: questo
esegue un'istruzione in 0.7ns, un incremento atomico richiede 58ns, acquisire
un *lock* che è nella memoria cache del processore richiede 160ns, e un
trasferimento dalla memoria cache di un altro processore richiede altri
170/360ns (Leggetevi l'articolo di Paul McKenney's `Linux Journal RCU
article <http://www.linuxjournal.com/article.php?sid=6993>`__).

Questi due obiettivi sono in conflitto: trattenere un *lock* per il minor
tempo possibile potrebbe richiedere la divisione in più *lock* per diverse
parti (come nel nostro ultimo esempio con un *lock* per ogni oggetto),
ma questo aumenta il numero di acquisizioni di *lock*, ed il risultato
spesso è che tutto è più lento che con un singolo *lock*. Questo è un altro
argomento in favore della semplicità quando si parla di sincronizzazione.

Il terzo punto è discusso di seguito: ci sono alcune tecniche per ridurre
il numero di sincronizzazioni che devono essere fatte.

Read/Write Lock Variants
------------------------

Sia gli spinlock che i mutex hanno una variante per la lettura/scrittura
(read/write): ``rwlock_t`` e :c:type:`struct rw_semaphore <rw_semaphore>`.
Queste dividono gli utenti in due categorie: i lettori e gli scrittori.
Se state solo leggendo i dati, potete acquisire il *lock* di lettura, ma
per scrivere avrete bisogno del *lock* di scrittura. Molti possono trattenere
il *lock* di lettura, ma solo uno scrittore alla volta può trattenere
quello di scrittura.

Se il vostro codice si divide chiaramente in codice per lettori e codice
per scrittori (come nel nostro esempio), e il *lock* dei lettori viene
trattenuto per molto tempo, allora l'uso di questo tipo di *lock* può aiutare.
Questi sono leggermente più lenti rispetto alla loro versione normale, quindi
nella pratica l'uso di ``rwlock_t`` non ne vale la pena.

Evitare i *lock*: Read Copy Update
--------------------------------------------

Esiste un metodo di sincronizzazione per letture e scritture detto
Read Copy Update. Con l'uso della tecnica RCU, i lettori possono scordarsi
completamente di trattenere i *lock*; dato che nel nostro esempio ci
aspettiamo d'avere più lettore che scrittori (altrimenti questa memoria
sarebbe uno spreco) possiamo dire che questo meccanismo permette
un'ottimizzazione.

Come facciamo a sbarazzarci dei *lock* di lettura? Sbarazzarsi dei *lock* di
lettura significa che uno scrittore potrebbe cambiare la lista sotto al naso
dei lettori. Questo è abbastanza semplice: possiamo leggere una lista
concatenata se lo scrittore aggiunge elementi alla fine e con certe
precauzioni. Per esempio, aggiungendo ``new`` ad una lista concatenata
chiamata ``list``::

            new->next = list->next;
            wmb();
            list->next = new;

La funzione wmb() è una barriera di sincronizzazione delle
scritture. Questa garantisce che la prima operazione (impostare l'elemento
``next`` del nuovo elemento) venga completata e vista da tutti i processori
prima che venga eseguita la seconda operazione (che sarebbe quella di mettere
il nuovo elemento nella lista). Questo è importante perché i moderni
compilatori ed i moderni processori possono, entrambe, riordinare le istruzioni
se non vengono istruiti altrimenti: vogliamo che i lettori non vedano
completamente il nuovo elemento; oppure che lo vedano correttamente e quindi
il puntatore ``next`` deve puntare al resto della lista.

Fortunatamente, c'è una funzione che fa questa operazione sulle liste
:c:type:`struct list_head <list_head>`: list_add_rcu()
(``include/linux/list.h``).

Rimuovere un elemento dalla lista è anche più facile: sostituiamo il puntatore
al vecchio elemento con quello del suo successore, e i lettori vedranno
l'elemento o lo salteranno.

::

            list->next = old->next;

La funzione list_del_rcu() (``include/linux/list.h``) fa esattamente
questo (la versione normale corrompe il vecchio oggetto, e non vogliamo che
accada).

Anche i lettori devono stare attenti: alcuni processori potrebbero leggere
attraverso il puntatore ``next`` il contenuto dell'elemento successivo
troppo presto, ma non accorgersi che il contenuto caricato è sbagliato quando
il puntatore ``next`` viene modificato alla loro spalle. Ancora una volta
c'è una funzione che viene in vostro aiuto list_for_each_entry_rcu()
(``include/linux/list.h``). Ovviamente, gli scrittori possono usare
list_for_each_entry() dato che non ci possono essere due scrittori
in contemporanea.

Il nostro ultimo dilemma è il seguente: quando possiamo realmente distruggere
l'elemento rimosso? Ricordate, un lettore potrebbe aver avuto accesso a questo
elemento proprio ora: se eliminiamo questo elemento ed il puntatore ``next``
cambia, il lettore salterà direttamente nella spazzatura e scoppierà. Dobbiamo
aspettare finché tutti i lettori che stanno attraversando la lista abbiano
finito. Utilizziamo call_rcu() per registrare una funzione di
richiamo che distrugga l'oggetto quando tutti i lettori correnti hanno
terminato. In alternative, potrebbe essere usata la funzione
synchronize_rcu() che blocca l'esecuzione finché tutti i lettori
non terminano di ispezionare la lista.

Ma come fa l'RCU a sapere quando i lettori sono finiti? Il meccanismo è
il seguente: innanzi tutto i lettori accedono alla lista solo fra la coppia
rcu_read_lock()/rcu_read_unlock() che disabilita la
prelazione così che i lettori non vengano sospesi mentre stanno leggendo
la lista.

Poi, l'RCU aspetta finché tutti i processori non abbiano dormito almeno
una volta; a questo punto, dato che i lettori non possono dormire, possiamo
dedurre che un qualsiasi lettore che abbia consultato la lista durante la
rimozione abbia già terminato, quindi la *callback* viene eseguita. Il vero
codice RCU è un po' più ottimizzato di così, ma questa è l'idea di fondo.

::

    --- cache.c.perobjectlock   2003-12-11 17:15:03.000000000 +1100
    +++ cache.c.rcupdate    2003-12-11 17:55:14.000000000 +1100
    @@ -1,15 +1,18 @@
     #include <linux/list.h>
     #include <linux/slab.h>
     #include <linux/string.h>
    +#include <linux/rcupdate.h>
     #include <linux/mutex.h>
     #include <asm/errno.h>

     struct object
     {
    -        /* These two protected by cache_lock. */
    +        /* This is protected by RCU */
             struct list_head list;
             int popularity;

    +        struct rcu_head rcu;
    +
             atomic_t refcnt;

             /* Doesn't change once created. */
    @@ -40,7 +43,7 @@
     {
             struct object *i;

    -        list_for_each_entry(i, &cache, list) {
    +        list_for_each_entry_rcu(i, &cache, list) {
                     if (i->id == id) {
                             i->popularity++;
                             return i;
    @@ -49,19 +52,25 @@
             return NULL;
     }

    +/* Final discard done once we know no readers are looking. */
    +static void cache_delete_rcu(void *arg)
    +{
    +        object_put(arg);
    +}
    +
     /* Must be holding cache_lock */
     static void __cache_delete(struct object *obj)
     {
             BUG_ON(!obj);
    -        list_del(&obj->list);
    -        object_put(obj);
    +        list_del_rcu(&obj->list);
             cache_num--;
    +        call_rcu(&obj->rcu, cache_delete_rcu);
     }

     /* Must be holding cache_lock */
     static void __cache_add(struct object *obj)
     {
    -        list_add(&obj->list, &cache);
    +        list_add_rcu(&obj->list, &cache);
             if (++cache_num > MAX_CACHE_SIZE) {
                     struct object *i, *outcast = NULL;
                     list_for_each_entry(i, &cache, list) {
    @@ -104,12 +114,11 @@
     struct object *cache_find(int id)
     {
             struct object *obj;
    -        unsigned long flags;

    -        spin_lock_irqsave(&cache_lock, flags);
    +        rcu_read_lock();
             obj = __cache_find(id);
             if (obj)
                     object_get(obj);
    -        spin_unlock_irqrestore(&cache_lock, flags);
    +        rcu_read_unlock();
             return obj;
     }

Da notare che i lettori modificano il campo popularity nella funzione
__cache_find(), e ora non trattiene alcun *lock*. Una soluzione
potrebbe essere quella di rendere la variabile ``atomic_t``, ma per l'uso
che ne abbiamo fatto qui, non ci interessano queste corse critiche perché un
risultato approssimativo è comunque accettabile, quindi non l'ho cambiato.

Il risultato è che la funzione cache_find() non ha bisogno di alcuna
sincronizzazione con le altre funzioni, quindi è veloce su un sistema
multi-processore tanto quanto lo sarebbe su un sistema mono-processore.

Esiste un'ulteriore ottimizzazione possibile: vi ricordate il codice originale
della nostra memoria dove non c'erano contatori di riferimenti e il chiamante
semplicemente tratteneva il *lock* prima di accedere ad un oggetto? Questo è
ancora possibile: se trattenete un *lock* nessuno potrà cancellare l'oggetto,
quindi non avete bisogno di incrementare e decrementare il contatore di
riferimenti.

Ora, dato che il '*lock* di lettura' di un RCU non fa altro che disabilitare
la prelazione, un chiamante che ha sempre la prelazione disabilitata fra le
chiamate cache_find() e object_put() non necessita
di incrementare e decrementare il contatore di riferimenti. Potremmo
esporre la funzione __cache_find() dichiarandola non-static,
e quel chiamante potrebbe usare direttamente questa funzione.

Il beneficio qui sta nel fatto che il contatore di riferimenti no
viene scritto: l'oggetto non viene alterato in alcun modo e quindi diventa
molto più veloce su sistemi molti-processore grazie alla loro memoria cache.


Dati per processore
-------------------

Un'altra tecnica comunemente usata per evitare la sincronizzazione è quella
di duplicare le informazioni per ogni processore. Per esempio, se volete
avere un contatore di qualcosa, potreste utilizzare uno spinlock ed un
singolo contatore. Facile e pulito.

Se questo dovesse essere troppo lento (solitamente non lo è, ma se avete
dimostrato che lo è devvero), potreste usare un contatore per ogni processore
e quindi non sarebbe più necessaria la mutua esclusione. Vedere
DEFINE_PER_CPU(), get_cpu_var() e put_cpu_var()
(``include/linux/percpu.h``).

Il tipo di dato ``local_t``, la funzione cpu_local_inc() e tutte
le altre funzioni associate, sono di particolare utilità per semplici contatori
per-processore; su alcune architetture sono anche più efficienti
(``include/asm/local.h``).

Da notare che non esiste un modo facile ed affidabile per ottenere il valore
di un simile contatore senza introdurre altri *lock*. In alcuni casi questo
non è un problema.

Dati che sono usati prevalentemente dai gestori d'interruzioni
--------------------------------------------------------------

Se i dati vengono utilizzati sempre dallo stesso gestore d'interruzioni,
allora i *lock* non vi servono per niente: il kernel già vi garantisce che
il gestore d'interruzione non verrà eseguito in contemporanea su diversi
processori.

Manfred Spraul fa notare che potreste comunque comportarvi così anche
se i dati vengono occasionalmente utilizzati da un contesto utente o
da un'interruzione software. Il gestore d'interruzione non utilizza alcun
*lock*, e tutti gli altri accessi verranno fatti così::

        spin_lock(&lock);
        disable_irq(irq);
        ...
        enable_irq(irq);
        spin_unlock(&lock);

La funzione disable_irq() impedisce al gestore d'interruzioni
d'essere eseguito (e aspetta che finisca nel caso fosse in esecuzione su
un altro processore). Lo spinlock, invece, previene accessi simultanei.
Naturalmente, questo è più lento della semplice chiamata
spin_lock_irq(), quindi ha senso solo se questo genere di accesso
è estremamente raro.


Quali funzioni possono essere chiamate in modo sicuro dalle interruzioni?
=========================================================================

Molte funzioni del kernel dormono (in sostanza, chiamano schedule())
direttamente od indirettamente: non potete chiamarle se trattenere uno
spinlock o avete la prelazione disabilitata, mai. Questo significa che
dovete necessariamente essere nel contesto utente: chiamarle da un
contesto d'interruzione è illegale.

Alcune funzioni che dormono
---------------------------

Le più comuni sono elencate qui di seguito, ma solitamente dovete leggere
il codice per scoprire se altre chiamate sono sicure. Se chiunque altro
le chiami dorme, allora dovreste poter dormire anche voi. In particolar
modo, le funzioni di registrazione e deregistrazione solitamente si
aspettano d'essere chiamante da un contesto utente e quindi che possono
dormire.

-  Accessi allo spazio utente:

   -  copy_from_user()

   -  copy_to_user()

   -  get_user()

   -  put_user()

-  kmalloc(GFP_KERNEL) <kmalloc>`

-  mutex_lock_interruptible() and
   mutex_lock()

   C'è anche mutex_trylock() che però non dorme.
   Comunque, non deve essere usata in un contesto d'interruzione dato
   che la sua implementazione non è sicura in quel contesto.
   Anche mutex_unlock() non dorme mai. Non può comunque essere
   usata in un contesto d'interruzione perché un mutex deve essere rilasciato
   dallo stesso processo che l'ha acquisito.

Alcune funzioni che non dormono
-------------------------------

Alcune funzioni possono essere chiamate tranquillamente da qualsiasi
contesto, o trattenendo un qualsiasi *lock*.

-  printk()

-  kfree()

-  add_timer() e del_timer()

Riferimento per l'API dei Mutex
===============================

.. kernel-doc:: include/linux/mutex.h
   :internal:

.. kernel-doc:: kernel/locking/mutex.c
   :export:

Riferimento per l'API dei Futex
===============================

.. kernel-doc:: kernel/futex/core.c
   :internal:

.. kernel-doc:: kernel/futex/futex.h
   :internal:

.. kernel-doc:: kernel/futex/pi.c
   :internal:

.. kernel-doc:: kernel/futex/requeue.c
   :internal:

.. kernel-doc:: kernel/futex/waitwake.c
   :internal:

Approfondimenti
===============

-  ``Documentation/locking/spinlocks.rst``: la guida di Linus Torvalds agli
   spinlock del kernel.

-  Unix Systems for Modern Architectures: Symmetric Multiprocessing and
   Caching for Kernel Programmers.

   L'introduzione alla sincronizzazione a livello di kernel di Curt Schimmel
   è davvero ottima (non è scritta per Linux, ma approssimativamente si adatta
   a tutte le situazioni). Il libro è costoso, ma vale ogni singolo spicciolo
   per capire la sincronizzazione nei sistemi multi-processore.
   [ISBN: 0201633388]

Ringraziamenti
==============

Grazie a Telsa Gwynne per aver formattato questa guida in DocBook, averla
pulita e aggiunto un po' di stile.

Grazie a Martin Pool, Philipp Rumpf, Stephen Rothwell, Paul Mackerras,
Ruedi Aschwanden, Alan Cox, Manfred Spraul, Tim Waugh, Pete Zaitcev,
James Morris, Robert Love, Paul McKenney, John Ashby per aver revisionato,
corretto, maledetto e commentato.

Grazie alla congrega per non aver avuto alcuna influenza su questo documento.

Glossario
=========

prelazione
  Prima del kernel 2.5, o quando ``CONFIG_PREEMPT`` non è impostato, i processi
  in contesto utente non si avvicendano nell'esecuzione (in pratica, il
  processo userà il processore fino al proprio termine, a meno che non ci siano
  delle interruzioni). Con l'aggiunta di ``CONFIG_PREEMPT`` nella versione
  2.5.4 questo è cambiato: quando si è in contesto utente, processi con una
  priorità maggiore possono subentrare nell'esecuzione: gli spinlock furono
  cambiati per disabilitare la prelazioni, anche su sistemi monoprocessore.

bh
  Bottom Half: per ragioni storiche, le funzioni che contengono '_bh' nel
  loro nome ora si riferiscono a qualsiasi interruzione software; per esempio,
  spin_lock_bh() blocca qualsiasi interuzione software sul processore
  corrente. I *Bottom Halves* sono deprecati, e probabilmente verranno
  sostituiti dai tasklet. In un dato momento potrà esserci solo un
  *bottom half* in esecuzione.

contesto d'interruzione
  Non è il contesto utente: qui si processano le interruzioni hardware e
  software. La macro in_interrupt() ritorna vero.

contesto utente
  Il kernel che esegue qualcosa per conto di un particolare processo (per
  esempio una chiamata di sistema) o di un thread del kernel. Potete
  identificare il processo con la macro ``current``. Da non confondere
  con lo spazio utente. Può essere interrotto sia da interruzioni software
  che hardware.

interruzione hardware
  Richiesta di interruzione hardware. in_hardirq() ritorna vero in un
  gestore d'interruzioni hardware.

interruzione software / softirq
  Gestore di interruzioni software: in_hardirq() ritorna falso;
  in_softirq() ritorna vero. I tasklet e le softirq sono entrambi
  considerati 'interruzioni software'.

  In soldoni, un softirq è uno delle 32 interruzioni software che possono
  essere eseguite su più processori in contemporanea. A volte si usa per
  riferirsi anche ai tasklet (in pratica tutte le interruzioni software).

monoprocessore / UP
  (Uni-Processor) un solo processore, ovvero non è SMP. (``CONFIG_SMP=n``).

multi-processore / SMP
  (Symmetric Multi-Processor) kernel compilati per sistemi multi-processore
  (``CONFIG_SMP=y``).

spazio utente
  Un processo che esegue il proprio codice fuori dal kernel.

tasklet
  Un'interruzione software registrabile dinamicamente che ha la garanzia
  d'essere eseguita solo su un processore alla volta.

timer
  Un'interruzione software registrabile dinamicamente che viene eseguita
  (circa) in un determinato momento. Quando è in esecuzione è come un tasklet
  (infatti, sono chiamati da ``TIMER_SOFTIRQ``).
