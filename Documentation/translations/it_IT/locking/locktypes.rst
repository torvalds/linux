.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-ita.rst

.. _it_kernel_hacking_locktypes:

========================================
Tipologie di blocco e le loro istruzioni
========================================

Introduzione
============

Il kernel fornisce un certo numero di primitive di blocco che possiamo dividere
in tre categorie:

  - blocchi ad attesa con sospensione
  - blocchi locali per CPU
  - blocchi ad attesa attiva

Questo documento descrive questi tre tipi e fornisce istruzioni su come
annidarli, ed usarli su kernel PREEMPT_RT.

Categorie di blocchi
====================

Blocchi ad attesa con sospensione
---------------------------------

I blocchi ad attesa con sospensione possono essere acquisiti solo in un contesti
dov'è possibile la prelazione.

Diverse implementazioni permettono di usare try_lock() anche in altri contesti,
nonostante ciò è bene considerare anche la sicurezza dei corrispondenti
unlock(). Inoltre, vanno prese in considerazione anche le varianti di *debug*
di queste primitive. Insomma, non usate i blocchi ad attesa con sospensioni in
altri contesti a meno che proprio non vi siano alternative.

In questa categoria troviamo:

 - mutex
 - rt_mutex
 - semaphore
 - rw_semaphore
 - ww_mutex
 - percpu_rw_semaphore

Nei kernel con PREEMPT_RT, i seguenti blocchi sono convertiti in blocchi ad
attesa con sospensione:

 - local_lock
 - spinlock_t
 - rwlock_t

Blocchi locali per CPU
----------------------

 - local_lock

Su kernel non-PREEMPT_RT, le funzioni local_lock gestiscono le primitive di
disabilitazione di prelazione ed interruzioni. Al contrario di altri meccanismi,
la disabilitazione della prelazione o delle interruzioni sono puri meccanismi
per il controllo della concorrenza su una CPU e quindi non sono adatti per la
gestione della concorrenza inter-CPU.

Blocchi ad attesa attiva
------------------------

 - raw_spinlcok_t
 - bit spinlocks

 Nei kernel non-PREEMPT_RT, i seguenti blocchi sono ad attesa attiva:

 - spinlock_t
 - rwlock_t

Implicitamente, i blocchi ad attesa attiva disabilitano la prelazione e le
funzioni lock/unlock hanno anche dei suffissi per gestire il livello di
protezione:

 ===================  =========================================================================
 _bh()                disabilita / abilita  *bottom halves* (interruzioni software)
 _irq()               disabilita / abilita le interruzioni
 _irqsave/restore()   salva e disabilita le interruzioni / ripristina ed attiva le interruzioni
 ===================  =========================================================================

Semantica del proprietario
==========================

Eccetto i semafori, i sopracitati tipi di blocchi hanno tutti una semantica
molto stringente riguardo al proprietario di un blocco:

  Il contesto (attività) che ha acquisito il blocco deve rilasciarlo

I semafori rw_semaphores hanno un'interfaccia speciale che permette anche ai non
proprietari del blocco di rilasciarlo per i lettori.

rtmutex
=======

I blocchi a mutua esclusione RT (*rtmutex*) sono un sistema a mutua esclusione
con supporto all'ereditarietà della priorità (PI).

Questo meccanismo ha delle limitazioni sui kernel non-PREEMPT_RT dovuti alla
prelazione e alle sezioni con interruzioni disabilitate.

Chiaramente, questo meccanismo non può avvalersi della prelazione su una sezione
dove la prelazione o le interruzioni sono disabilitate; anche sui kernel
PREEMPT_RT. Tuttavia, i kernel PREEMPT_RT eseguono la maggior parte delle
sezioni in contesti dov'è possibile la prelazione, specialmente in contesti
d'interruzione (anche software). Questa conversione permette a spinlock_t e
rwlock_t di essere implementati usando rtmutex.

semaphore
=========

La primitiva semaphore implementa un semaforo con contatore.

I semafori vengono spesso utilizzati per la serializzazione e l'attesa, ma per
nuovi casi d'uso si dovrebbero usare meccanismi diversi, come mutex e
completion.

semaphore e PREEMPT_RT
----------------------

I kernel PREEMPT_RT non cambiano l'implementazione di semaphore perché non hanno
un concetto di proprietario, dunque impediscono a PREEMPT_RT d'avere
l'ereditarietà della priorità sui semafori. Un proprietario sconosciuto non può
ottenere una priorità superiore. Di consequenza, bloccarsi sui semafori porta
all'inversione di priorità.


rw_semaphore
============

Il blocco rw_semaphore è un meccanismo che permette più lettori ma un solo scrittore.

Sui kernel non-PREEMPT_RT l'implementazione è imparziale, quindi previene
l'inedia dei processi scrittori.

Questi blocchi hanno una semantica molto stringente riguardo il proprietario, ma
offre anche interfacce speciali che permettono ai processi non proprietari di
rilasciare un processo lettore. Queste interfacce funzionano indipendentemente
dalla configurazione del kernel.

rw_semaphore e PREEMPT_RT
-------------------------

I kernel PREEMPT_RT sostituiscono i rw_semaphore con un'implementazione basata
su rt_mutex, e questo ne modifica l'imparzialità:

 Dato che uno scrittore rw_semaphore non può assicurare la propria priorità ai
 suoi lettori, un lettore con priorità più bassa che ha subito la prelazione
 continuerà a trattenere il blocco, quindi porta all'inedia anche gli scrittori
 con priorità più alta. Per contro, dato che i lettori possono garantire la
 propria priorità agli scrittori, uno scrittore a bassa priorità che subisce la
 prelazione vedrà la propria priorità alzata finché non rilascerà il blocco, e
 questo preverrà l'inedia dei processi lettori a causa di uno scrittore.


local_lock
==========

I local_lock forniscono nomi agli ambiti di visibilità delle sezioni critiche
protette tramite la disattivazione della prelazione o delle interruzioni.

Sui kernel non-PREEMPT_RT le operazioni local_lock si traducono
nell'abilitazione o disabilitazione della prelazione o le interruzioni.

 ===============================  ======================
 local_lock(&llock)               preempt_disable()
 local_unlock(&llock)             preempt_enable()
 local_lock_irq(&llock)           local_irq_disable()
 local_unlock_irq(&llock)         local_irq_enable()
 local_lock_irqsave(&llock)       local_irq_save()
 local_unlock_irqrestore(&llock)  local_irq_restore()
 ===============================  ======================

Gli ambiti di visibilità con nome hanno due vantaggi rispetto alle primitive di
base:

  - Il nome del blocco permette di fare un'analisi statica, ed è anche chiaro su
    cosa si applichi la protezione cosa che invece non si può fare con le
    classiche primitive in quanto sono opache e senza alcun ambito di
    visibilità.

  - Se viene abilitato lockdep, allora local_lock ottiene un lockmap che
    permette di verificare la bontà della protezione. Per esempio, questo può
    identificare i casi dove una funzione usa preempt_disable() come meccanismo
    di protezione in un contesto d'interruzione (anche software). A parte
    questo, lockdep_assert_held(&llock) funziona come tutte le altre primitive
    di sincronizzazione.

local_lock e PREEMPT_RT
-------------------------

I kernel PREEMPT_RT sostituiscono local_lock con uno spinlock_t per CPU, quindi
ne cambia la semantica:

  - Tutte le modifiche a spinlock_t si applicano anche a local_lock

L'uso di local_lock
-------------------

I local_lock dovrebbero essere usati su kernel non-PREEMPT_RT quando la
disabilitazione della prelazione o delle interruzioni è il modo più adeguato per
gestire l'accesso concorrente a strutture dati per CPU.

Questo meccanismo non è adatto alla protezione da prelazione o interruzione su
kernel PREEMPT_RT dato che verrà convertito in spinlock_t.


raw_spinlock_t e spinlock_t
===========================

raw_spinlock_t
--------------

I blocco raw_spinlock_t è un blocco ad attesa attiva su tutti i tipi di kernel,
incluso quello PREEMPT_RT. Usate raw_spinlock_t solo in sezioni critiche nel
cuore del codice, nella gestione delle interruzioni di basso livello, e in posti
dove è necessario disabilitare la prelazione o le interruzioni. Per esempio, per
accedere in modo sicuro lo stato dell'hardware. A volte, i raw_spinlock_t
possono essere usati quando la sezione critica è minuscola, per evitare gli
eccessi di un rtmutex.

spinlock_t
----------

Il significato di spinlock_t cambia in base allo stato di PREEMPT_RT.

Sui kernel non-PREEMPT_RT, spinlock_t si traduce in un raw_spinlock_t ed ha
esattamente lo stesso significato.

spinlock_t e PREEMPT_RT
-----------------------

Sui kernel PREEMPT_RT, spinlock_t ha un'implementazione dedicata che si basa
sull'uso di rt_mutex. Questo ne modifica il significato:

 - La prelazione non viene disabilitata.

 - I suffissi relativi alla interruzioni (_irq, _irqsave / _irqrestore) per le
   operazioni spin_lock / spin_unlock non hanno alcun effetto sullo stato delle
   interruzioni della CPU.

 - I suffissi relativi alle interruzioni software (_bh()) disabilitano i
   relativi gestori d'interruzione.

   I kernel non-PREEMPT_RT disabilitano la prelazione per ottenere lo stesso effetto.

   I kernel PREEMPT_RT usano un blocco per CPU per la serializzazione, il che
   permette di tenere attiva la prelazione. Il blocco disabilita i gestori
   d'interruzione software e previene la rientranza vista la prelazione attiva.

A parte quanto appena discusso, i kernel PREEMPT_RT preservano il significato
di tutti gli altri aspetti di spinlock_t:

 - Le attività che trattengono un blocco spinlock_t non migrano su altri
   processori. Disabilitando la prelazione, i kernel non-PREEMPT_RT evitano la
   migrazione. Invece, i kernel PREEMPT_RT disabilitano la migrazione per
   assicurarsi che i puntatori a variabili per CPU rimangano validi anche
   quando un'attività subisce la prelazione.

 - Lo stato di un'attività si mantiene durante le acquisizioni del blocco al
   fine di garantire che le regole basate sullo stato delle attività si possano
   applicare a tutte le configurazioni del kernel. I kernel non-PREEMPT_RT
   lasciano lo stato immutato. Tuttavia, la funzionalità PREEMPT_RT deve
   cambiare lo stato se l'attività si blocca durante l'acquisizione. Dunque,
   salva lo stato attuale prima di bloccarsi ed il rispettivo risveglio lo
   ripristinerà come nell'esempio seguente::

    task->state = TASK_INTERRUPTIBLE
     lock()
       block()
         task->saved_state = task->state
	 task->state = TASK_UNINTERRUPTIBLE
	 schedule()
					lock wakeup
					  task->state = task->saved_state

   Altri tipi di risvegli avrebbero impostato direttamente lo stato a RUNNING,
   ma in questo caso non avrebbe funzionato perché l'attività deve rimanere
   bloccata fintanto che il blocco viene trattenuto. Quindi, lo stato salvato
   viene messo a RUNNING quando il risveglio di un non-blocco cerca di
   risvegliare un'attività bloccata in attesa del rilascio di uno spinlock. Poi,
   quando viene completata l'acquisizione del blocco, il suo risveglio
   ripristinerà lo stato salvato, in questo caso a RUNNING::

    task->state = TASK_INTERRUPTIBLE
     lock()
       block()
         task->saved_state = task->state
	 task->state = TASK_UNINTERRUPTIBLE
	 schedule()
					non lock wakeup
					  task->saved_state = TASK_RUNNING

					lock wakeup
					  task->state = task->saved_state

   Questo garantisce che il vero risveglio non venga perso.

rwlock_t
========

Il blocco rwlock_t è un meccanismo che permette più lettori ma un solo scrittore.

Sui kernel non-PREEMPT_RT questo è un blocco ad attesa e per i suoi suffissi si
applicano le stesse regole per spinlock_t. La sua implementazione è imparziale,
quindi previene l'inedia dei processi scrittori.

rwlock_t e PREEMPT_RT
---------------------

Sui kernel PREEMPT_RT rwlock_t ha un'implementazione dedicata che si basa
sull'uso di rt_mutex. Questo ne modifica il significato:

 - Tutte le modifiche fatte a spinlock_t si applicano anche a rwlock_t.

 - Dato che uno scrittore rw_semaphore non può assicurare la propria priorità ai
   suoi lettori, un lettore con priorità più bassa che ha subito la prelazione
   continuerà a trattenere il blocco, quindi porta all'inedia anche gli
   scrittori con priorità più alta. Per contro, dato che i lettori possono
   garantire la propria priorità agli scrittori, uno scrittore a bassa priorità
   che subisce la prelazione vedrà la propria priorità alzata finché non
   rilascerà il blocco, e questo preverrà l'inedia dei processi lettori a causa
   di uno scrittore.


Precisazioni su PREEMPT_RT
==========================

local_lock su RT
----------------

Sui kernel PREEMPT_RT Ci sono alcune implicazioni dovute alla conversione di
local_lock in un spinlock_t. Per esempio, su un kernel non-PREEMPT_RT il
seguente codice funzionerà come ci si aspetta::

  local_lock_irq(&local_lock);
  raw_spin_lock(&lock);

ed è equivalente a::

   raw_spin_lock_irq(&lock);

Ma su un kernel PREEMPT_RT questo codice non funzionerà perché local_lock_irq()
si traduce in uno spinlock_t per CPU che non disabilita né le interruzioni né la
prelazione. Il seguente codice funzionerà su entrambe i kernel con o senza
PREEMPT_RT::

  local_lock_irq(&local_lock);
  spin_lock(&lock);

Un altro dettaglio da tenere a mente con local_lock è che ognuno di loro ha un
ambito di protezione ben preciso. Dunque, la seguente sostituzione è errate::


  func1()
  {
    local_irq_save(flags);    -> local_lock_irqsave(&local_lock_1, flags);
    func3();
    local_irq_restore(flags); -> local_unlock_irqrestore(&local_lock_1, flags);
  }

  func2()
  {
    local_irq_save(flags);    -> local_lock_irqsave(&local_lock_2, flags);
    func3();
    local_irq_restore(flags); -> local_unlock_irqrestore(&local_lock_2, flags);
  }

  func3()
  {
    lockdep_assert_irqs_disabled();
    access_protected_data();
  }

Questo funziona correttamente su un kernel non-PREEMPT_RT, ma su un kernel
PREEMPT_RT local_lock_1 e local_lock_2 sono distinti e non possono serializzare
i chiamanti di func3(). L'*assert* di lockdep verrà attivato su un kernel
PREEMPT_RT perché local_lock_irqsave() non disabilita le interruzione a casa
della specifica semantica di spinlock_t in PREEMPT_RT. La corretta sostituzione
è::

  func1()
  {
    local_irq_save(flags);    -> local_lock_irqsave(&local_lock, flags);
    func3();
    local_irq_restore(flags); -> local_unlock_irqrestore(&local_lock, flags);
  }

  func2()
  {
    local_irq_save(flags);    -> local_lock_irqsave(&local_lock, flags);
    func3();
    local_irq_restore(flags); -> local_unlock_irqrestore(&local_lock, flags);
  }

  func3()
  {
    lockdep_assert_held(&local_lock);
    access_protected_data();
  }

spinlock_t e rwlock_t
---------------------

Ci sono alcune conseguenze di cui tener conto dal cambiamento di semantica di
spinlock_t e rwlock_t sui kernel PREEMPT_RT. Per esempio, sui kernel non
PREEMPT_RT il seguente codice funziona come ci si aspetta::

   local_irq_disable();
   spin_lock(&lock);

ed è equivalente a::

   spin_lock_irq(&lock);

Lo stesso vale per rwlock_t e le varianti con _irqsave().

Sui kernel PREEMPT_RT questo codice non funzionerà perché gli rtmutex richiedono
un contesto con la possibilità di prelazione. Al suo posto, usate
spin_lock_irq() o spin_lock_irqsave() e le loro controparti per il rilascio. I
kernel PREEMPT_RT offrono un meccanismo local_lock per i casi in cui la
disabilitazione delle interruzioni ed acquisizione di un blocco devono rimanere
separati. Acquisire un local_lock àncora un processo ad una CPU permettendo cose
come un'acquisizione di un blocco con interruzioni disabilitate per singola CPU.

Il tipico scenario è quando si vuole proteggere una variabile di processore nel
contesto di un thread::


  struct foo *p = get_cpu_ptr(&var1);

  spin_lock(&p->lock);
  p->count += this_cpu_read(var2);

Questo codice è corretto su un kernel non-PREEMPT_RT, ma non lo è su un
PREEMPT_RT. La modifica della semantica di spinlock_t su PREEMPT_RT non permette
di acquisire p->lock perché, implicitamente, get_cpu_ptr() disabilita la
prelazione. La seguente sostituzione funzionerà su entrambe i kernel::

  struct foo *p;

  migrate_disable();
  p = this_cpu_ptr(&var1);
  spin_lock(&p->lock);
  p->count += this_cpu_read(var2);

La funzione migrate_disable() assicura che il processo venga tenuto sulla CPU
corrente, e di conseguenza garantisce che gli accessi per-CPU alle variabili var1 e
var2 rimangano sulla stessa CPU fintanto che il processo rimane prelabile.

La sostituzione con migrate_disable() non funzionerà nel seguente scenario::

  func()
  {
    struct foo *p;

    migrate_disable();
    p = this_cpu_ptr(&var1);
    p->val = func2();

Questo non funziona perché migrate_disable() non protegge dal ritorno da un
processo che aveva avuto il diritto di prelazione. Una sostituzione più adatta
per questo caso è::

  func()
  {
    struct foo *p;

    local_lock(&foo_lock);
    p = this_cpu_ptr(&var1);
    p->val = func2();

Su un kernel non-PREEMPT_RT, questo codice protegge dal rientro disabilitando la
prelazione. Su un kernel PREEMPT_RT si ottiene lo stesso risultato acquisendo lo
spinlock di CPU.

raw_spinlock_t su RT
--------------------

Acquisire un raw_spinlock_t disabilita la prelazione e possibilmente anche le
interruzioni, quindi la sezione critica deve evitare di acquisire uno spinlock_t
o rwlock_t. Per esempio, la sezione critica non deve fare allocazioni di
memoria. Su un kernel non-PREEMPT_RT il seguente codice funziona perfettamente::

  raw_spin_lock(&lock);
  p = kmalloc(sizeof(*p), GFP_ATOMIC);

Ma lo stesso codice non funziona su un kernel PREEMPT_RT perché l'allocatore di
memoria può essere oggetto di prelazione e quindi non può essere chiamato in un
contesto atomico. Tuttavia, si può chiamare l'allocatore di memoria quando si
trattiene un blocco *non-raw* perché non disabilitano la prelazione sui kernel
PREEMPT_RT::

  spin_lock(&lock);
  p = kmalloc(sizeof(*p), GFP_ATOMIC);


bit spinlocks
-------------

I kernel PREEMPT_RT non possono sostituire i bit spinlock perché un singolo bit
è troppo piccolo per farci stare un rtmutex. Dunque, la semantica dei bit
spinlock è mantenuta anche sui kernel PREEMPT_RT. Quindi, le precisazioni fatte
per raw_spinlock_t valgono anche qui.

In PREEMPT_RT, alcuni bit spinlock sono sostituiti con normali spinlock_t usando
condizioni di preprocessore in base a dove vengono usati. Per contro, questo non
serve quando si sostituiscono gli spinlock_t. Invece, le condizioni poste sui
file d'intestazione e sul cuore dell'implementazione della sincronizzazione
permettono al compilatore di effettuare la sostituzione in modo trasparente.


Regole d'annidamento dei tipi di blocchi
========================================

Le regole principali sono:

  - I tipi di blocco appartenenti alla stessa categoria possono essere annidati
    liberamente a patto che si rispetti l'ordine di blocco al fine di evitare
    stalli.

  - I blocchi con sospensione non possono essere annidati in blocchi del tipo
    CPU locale o ad attesa attiva

  - I blocchi ad attesa attiva e su CPU locale possono essere annidati nei
    blocchi ad attesa con sospensione.

  - I blocchi ad attesa attiva possono essere annidati in qualsiasi altro tipo.

Queste limitazioni si applicano ad entrambe i kernel con o senza PREEMPT_RT.

Il fatto che un kernel PREEMPT_RT cambi i blocchi spinlock_t e rwlock_t dal tipo
ad attesa attiva a quello con sospensione, e che sostituisca local_lock con uno
spinlock_t per CPU, significa che non possono essere acquisiti quando si è in un
blocco raw_spinlock. Ne consegue il seguente ordine d'annidamento:

  1) blocchi ad attesa con sospensione
  2) spinlock_t, rwlock_t, local_lock
  3) raw_spinlock_t e bit spinlocks

Se queste regole verranno violate, allora lockdep se ne accorgerà e questo sia
con o senza PREEMPT_RT.
