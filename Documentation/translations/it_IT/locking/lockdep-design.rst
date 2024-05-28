.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-ita.rst

Validatore di sincronizzazione durante l'esecuzione
===================================================

Classi di blocchi
-----------------

L'oggetto su cui il validatore lavora è una "classe" di blocchi.

Una classe di blocchi è un gruppo di blocchi che seguono le stesse regole di
sincronizzazione, anche quando i blocchi potrebbero avere più istanze (anche
decine di migliaia). Per esempio un blocco nella struttura inode è una classe,
mentre ogni inode sarà un'istanza di questa classe di blocco.

Il validatore traccia lo "stato d'uso" di una classe di blocchi e le sue
dipendenze con altre classi. L'uso di un blocco indica come quel blocco viene
usato rispetto al suo contesto d'interruzione, mentre le dipendenze di un blocco
possono essere interpretate come il loro ordine; per esempio L1 -> L2 suggerisce
che un processo cerca di acquisire L2 mentre già trattiene L1. Dal punto di
vista di lockdep, i due blocchi (L1 ed L2) non sono per forza correlati: quella
dipendenza indica solamente l'ordine in cui sono successe le cose. Il validatore
verifica permanentemente la correttezza dell'uso dei blocchi e delle loro
dipendenze, altrimenti ritornerà un errore.

Il comportamento di una classe di blocchi viene costruito dall'insieme delle sue
istanze. Una classe di blocco viene registrata alla creazione della sua prima
istanza, mentre tutte le successive istanze verranno mappate; dunque, il loro
uso e le loro dipendenze contribuiranno a costruire quello della classe. Una
classe di blocco non sparisce quando sparisce una sua istanza, ma può essere
rimossa quando il suo spazio in memoria viene reclamato. Per esempio, questo
succede quando si rimuove un modulo, o quando una *workqueue* viene eliminata.

Stato
-----

Il validatore traccia l'uso cronologico delle classi di blocchi e ne divide
l'uso in categorie (4 USI * n STATI + 1).

I quattro USI possono essere:

- 'sempre trattenuto nel contesto <STATO>'
- 'sempre trattenuto come blocco di lettura nel contesto <STATO>'
- 'sempre trattenuto con <STATO> abilitato'
- 'sempre trattenuto come blocco di lettura con <STATO> abilitato'

gli `n` STATI sono codificati in kernel/locking/lockdep_states.h, ad oggi
includono:

- hardirq
- softirq

infine l'ultima categoria è:

- 'sempre trattenuto'                                  [ == !unused        ]

Quando vengono violate le regole di sincronizzazione, questi bit di utilizzo
vengono presentati nei messaggi di errore di sincronizzazione, fra parentesi
graffe, per un totale di `2 * n` (`n`: bit STATO). Un esempio inventato::

   modprobe/2287 is trying to acquire lock:
    (&sio_locks[i].lock){-.-.}, at: [<c02867fd>] mutex_lock+0x21/0x24

   but task is already holding lock:
    (&sio_locks[i].lock){-.-.}, at: [<c02867fd>] mutex_lock+0x21/0x24

Per un dato blocco, da sinistra verso destra, la posizione del bit indica l'uso
del blocco e di un eventuale blocco di lettura, per ognuno degli `n` STATI elencati
precedentemente. Il carattere mostrato per ogni bit indica:

   ===  ===========================================================================
   '.'  acquisito con interruzioni disabilitate fuori da un contesto d'interruzione
   '-'  acquisito in contesto d'interruzione
   '+'  acquisito con interruzioni abilitate
   '?'  acquisito in contesto d'interruzione con interruzioni abilitate
   ===  ===========================================================================

Il seguente esempio mostra i bit::

    (&sio_locks[i].lock){-.-.}, at: [<c02867fd>] mutex_lock+0x21/0x24
                         ||||
                         ||| \-> softirq disabilitati e fuori da un contesto di softirq
                         || \--> acquisito in un contesto di softirq
                         | \---> hardirq disabilitati e fuori da un contesto di hardirq
                          \----> acquisito in un contesto di hardirq

Per un dato STATO, che il blocco sia mai stato acquisito in quel contesto di
STATO, o che lo STATO sia abilitato, ci lascia coi quattro possibili scenari
mostrati nella seguente tabella. Il carattere associato al bit indica con
esattezza in quale scenario ci si trova al momento del rapporto.

  +---------------+---------------+------------------+
  |               | irq abilitati | irq disabilitati |
  +---------------+---------------+------------------+
  | sempre in irq |      '?'      |       '-'        |
  +---------------+---------------+------------------+
  | mai in irq    |      '+'      |       '.'        |
  +---------------+---------------+------------------+

Il carattere '-' suggerisce che le interruzioni sono disabilitate perché
altrimenti verrebbe mostrato il carattere '?'. Una deduzione simile può essere
fatta anche per '+'

I blocchi inutilizzati (ad esempio i mutex) non possono essere fra le cause di
un errore.

Regole dello stato per un blocco singolo
----------------------------------------

Avere un blocco sicuro in interruzioni (*irq-safe*) significa che è sempre stato
usato in un contesto d'interruzione, mentre un blocco insicuro in interruzioni
(*irq-unsafe*) significa che è sempre stato acquisito con le interruzioni
abilitate.

Una classe softirq insicura è automaticamente insicura anche per hardirq. I
seguenti stati sono mutualmente esclusivi: solo una può essere vero quando viene
usata una classe di blocco::

 <hardirq-safe> o <hardirq-unsafe>
 <softirq-safe> o <softirq-unsafe>

Questo perché se un blocco può essere usato in un contesto di interruzioni
(sicuro in interruzioni), allora non può mai essere acquisito con le
interruzioni abilitate (insicuro in interruzioni). Altrimenti potrebbe
verificarsi uno stallo. Per esempio, questo blocco viene acquisito, ma prima di
essere rilasciato il contesto d'esecuzione viene interrotto nuovamente, e quindi
si tenterà di acquisirlo nuovamente. Questo porterà ad uno stallo, in
particolare uno stallo ricorsivo.

Il validatore rileva e riporta gli usi di blocchi che violano queste regole per
blocchi singoli.

Regole per le dipendenze di blocchi multipli
--------------------------------------------

La stessa classe di blocco non deve essere acquisita due volte, questo perché
potrebbe portare ad uno blocco ricorsivo e dunque ad uno stallo.

Inoltre, due blocchi non possono essere trattenuti in ordine inverso::

 <L1> -> <L2>
 <L2> -> <L1>

perché porterebbe ad uno stallo - chiamato stallo da blocco inverso - in cui si
cerca di trattenere i due blocchi in un ciclo in cui entrambe i contesti
aspettano per sempre che l'altro termini. Il validatore è in grado di trovare
queste dipendenze cicliche di qualsiasi complessità, ovvero nel mezzo ci
potrebbero essere altre sequenze di blocchi. Il validatore troverà se questi
blocchi possono essere acquisiti circolarmente.

In aggiunta, le seguenti sequenze di blocco nei contesti indicati non sono
permesse, indipendentemente da quale che sia la classe di blocco::

   <hardirq-safe>   ->  <hardirq-unsafe>
   <softirq-safe>   ->  <softirq-unsafe>

La prima regola deriva dal fatto che un blocco sicuro in interruzioni può essere
trattenuto in un contesto d'interruzione che, per definizione, ha la possibilità
di interrompere un blocco insicuro in interruzioni; questo porterebbe ad uno
stallo da blocco inverso. La seconda, analogamente, ci dice che un blocco sicuro
in interruzioni software potrebbe essere trattenuto in un contesto di
interruzione software, dunque potrebbe interrompere un blocco insicuro in
interruzioni software.

Le suddette regole vengono applicate per qualsiasi sequenza di blocchi: quando
si acquisiscono nuovi blocchi, il validatore verifica se vi è una violazione
delle regole fra il nuovo blocco e quelli già trattenuti.

Quando una classe di blocco cambia stato, applicheremo le seguenti regole:

- se viene trovato un nuovo blocco sicuro in interruzioni, verificheremo se
  abbia mai trattenuto dei blocchi insicuri in interruzioni.

- se viene trovato un nuovo blocco sicuro in interruzioni software,
  verificheremo se abbia trattenuto dei blocchi insicuri in interruzioni
  software.

- se viene trovato un nuovo blocco insicuro in interruzioni, verificheremo se
  abbia trattenuto dei blocchi sicuri in interruzioni.

- se viene trovato un nuovo blocco insicuro in interruzioni software,
  verificheremo se abbia trattenuto dei blocchi sicuri in interruzioni
  software.

(Di nuovo, questi controlli vengono fatti perché un contesto d'interruzione
potrebbe interrompere l'esecuzione di qualsiasi blocco insicuro portando ad uno
stallo; questo anche se lo stallo non si verifica in pratica)

Eccezione: dipendenze annidate sui dati portano a blocchi annidati
------------------------------------------------------------------

Ci sono alcuni casi in cui il kernel Linux acquisisce più volte la stessa
istanza di una classe di blocco. Solitamente, questo succede quando esiste una
gerarchia fra oggetti dello stesso tipo. In questi casi viene ereditato
implicitamente l'ordine fra i due oggetti (definito dalle proprietà di questa
gerarchia), ed il kernel tratterrà i blocchi in questo ordine prefissato per
ognuno degli oggetti.

Un esempio di questa gerarchia di oggetti che producono "blocchi annidati" sono
i *block-dev* che rappresentano l'intero disco e quelli che rappresentano una
sua partizione; la partizione è una parte del disco intero, e l'ordine dei
blocchi sarà corretto fintantoche uno acquisisce il blocco del disco intero e
poi quello della partizione. Il validatore non rileva automaticamente questo
ordine implicito, perché queste regole di sincronizzazione non sono statiche.

Per istruire il validatore riguardo a questo uso corretto dei blocchi sono stati
introdotte nuove primitive per specificare i "livelli di annidamento". Per
esempio, per i blocchi a mutua esclusione dei *block-dev* si avrebbe una
chiamata simile a::

  enum bdev_bd_mutex_lock_class
  {
       BD_MUTEX_NORMAL,
       BD_MUTEX_WHOLE,
       BD_MUTEX_PARTITION
  };

  mutex_lock_nested(&bdev->bd_contains->bd_mutex, BD_MUTEX_PARTITION);

In questo caso la sincronizzazione viene fatta su un *block-dev* sapendo che si
tratta di una partizione.

Ai fini della validazione, il validatore lo considererà con una - sotto - classe
di blocco separata.

Nota: Prestate estrema attenzione che la vostra gerarchia sia corretta quando si
vogliono usare le primitive _nested(); altrimenti potreste avere sia falsi
positivi che falsi negativi.

Annotazioni
-----------

Si possono utilizzare due costrutti per verificare ed annotare se certi blocchi
devono essere trattenuti: lockdep_assert_held*(&lock) e
lockdep_*pin_lock(&lock).

Come suggerito dal nome, la famiglia di macro lockdep_assert_held* asseriscono
che un dato blocco in un dato momento deve essere trattenuto (altrimenti, verrà
generato un WARN()). Queste vengono usate abbondantemente nel kernel, per
esempio in kernel/sched/core.c::

  void update_rq_clock(struct rq *rq)
  {
	s64 delta;

	lockdep_assert_held(&rq->lock);
	[...]
  }

dove aver trattenuto rq->lock è necessario per aggiornare in sicurezza il clock
rq.

L'altra famiglia di macro è lockdep_*pin_lock(), che a dire il vero viene usata
solo per rq->lock ATM. Se per caso un blocco non viene trattenuto, queste
genereranno un WARN(). Questo si rivela particolarmente utile quando si deve
verificare la correttezza di codice con *callback*, dove livelli superiori
potrebbero assumere che un blocco rimanga trattenuto, ma livelli inferiori
potrebbero invece pensare che il blocco possa essere rilasciato e poi
riacquisito (involontariamente si apre una sezione critica). lockdep_pin_lock()
restituisce 'struct pin_cookie' che viene usato da lockdep_unpin_lock() per
verificare che nessuno abbia manomesso il blocco. Per esempio in
kernel/sched/sched.h abbiamo::

  static inline void rq_pin_lock(struct rq *rq, struct rq_flags *rf)
  {
	rf->cookie = lockdep_pin_lock(&rq->lock);
	[...]
  }

  static inline void rq_unpin_lock(struct rq *rq, struct rq_flags *rf)
  {
	[...]
	lockdep_unpin_lock(&rq->lock, rf->cookie);
  }

I commenti riguardo alla sincronizzazione possano fornire informazioni utili,
tuttavia sono le verifiche in esecuzione effettuate da queste macro ad essere
vitali per scovare problemi di sincronizzazione, ed inoltre forniscono lo stesso
livello di informazioni quando si ispeziona il codice. Nel dubbio, preferite
queste annotazioni!

Dimostrazione di correttezza al 100%
------------------------------------

Il validatore verifica la proprietà di chiusura in senso matematico. Ovvero, per
ogni sequenza di sincronizzazione di un singolo processo che si verifichi almeno
una volta nel kernel, il validatore dimostrerà con una certezza del 100% che
nessuna combinazione e tempistica di queste sequenze possa causare uno stallo in
una qualsiasi classe di blocco. [1]_

In pratica, per dimostrare l'esistenza di uno stallo non servono complessi
scenari di sincronizzazione multi-processore e multi-processo. Il validatore può
dimostrare la correttezza basandosi sulla sola sequenza di sincronizzazione
apparsa almeno una volta (in qualunque momento, in qualunque processo o
contesto). Uno scenario complesso che avrebbe bisogno di 3 processori e una
sfortunata presenza di processi, interruzioni, e pessimo tempismo, può essere
riprodotto su un sistema a singolo processore.

Questo riduce drasticamente la complessità del controllo di qualità della
sincronizzazione nel kernel: quello che deve essere fatto è di innescare nel
kernel quante più possibili "semplici" sequenze di sincronizzazione, almeno una
volta, allo scopo di dimostrarne la correttezza. Questo al posto di innescare
una verifica per ogni possibile combinazione di sincronizzazione fra processori,
e differenti scenari con hardirq e softirq e annidamenti vari (nella pratica,
impossibile da fare)

.. [1]

   assumendo che il validatore sia corretto al 100%, e che nessun altra parte
   del sistema possa corromperne lo stato. Assumiamo anche che tutti i percorsi
   MNI/SMM [potrebbero interrompere anche percorsi dove le interruzioni sono
   disabilitate] sono corretti e non interferiscono con il validatore. Inoltre,
   assumiamo che un hash a 64-bit sia unico per ogni sequenza di
   sincronizzazione nel sistema. Infine, la ricorsione dei blocchi non deve
   essere maggiore di 20.

Prestazione
-----------

Le regole sopracitate hanno bisogno di una quantità **enorme** di verifiche
durante l'esecuzione. Il sistema sarebbe diventato praticamente inutilizzabile
per la sua lentezza se le avessimo fatte davvero per ogni blocco trattenuto e
per ogni abilitazione delle interruzioni. La complessità della verifica è
O(N^2), quindi avremmo dovuto fare decine di migliaia di verifiche per ogni
evento, il tutto per poche centinaia di classi.

Il problema è stato risolto facendo una singola verifica per ogni 'scenario di
sincronizzazione' (una sequenza unica di blocchi trattenuti uno dopo l'altro).
Per farlo, viene mantenuta una pila dei blocchi trattenuti, e viene calcolato un
hash a 64-bit unico per ogni sequenza. Quando la sequenza viene verificata per
la prima volta, l'hash viene inserito in una tabella hash. La tabella potrà
essere verificata senza bisogno di blocchi. Se la sequenza dovesse ripetersi, la
tabella ci dirà che non è necessario verificarla nuovamente.

Risoluzione dei problemi
------------------------

Il massimo numero di classi di blocco che il validatore può tracciare è:
MAX_LOCKDEP_KEYS. Oltrepassare questo limite indurrà lokdep a generare il
seguente avviso::

	(DEBUG_LOCKS_WARN_ON(id >= MAX_LOCKDEP_KEYS))

Di base questo valore è 8191, e un classico sistema da ufficio ha meno di 1000
classi, dunque questo avviso è solitamente la conseguenza di un problema di
perdita delle classi di blocco o d'inizializzazione dei blocchi. Di seguito una
descrizione dei due problemi:

1. caricare e rimuovere continuamente i moduli mentre il validatore è in
   esecuzione porterà ad una perdita di classi di blocco. Il problema è che ogni
   caricamento crea un nuovo insieme di classi di blocco per tutti i blocchi di
   quel modulo. Tuttavia, la rimozione del modulo non rimuove le vecchie classi
   (vedi dopo perché non le riusiamo). Dunque, il continuo caricamento e
   rimozione di un modulo non fa altro che aumentare il contatore di classi fino
   a raggiungere, eventualmente, il limite.

2. Usare array con un gran numero di blocchi che non vengono esplicitamente
   inizializzati. Per esempio, una tabella hash con 8192 *bucket* dove ognuno ha
   il proprio spinlock_t consumerà 8192 classi di blocco a meno che non vengano
   esplicitamente inizializzati in esecuzione usando spin_lock_init() invece
   dell'inizializzazione durante la compilazione con __SPIN_LOCK_UNLOCKED().
   Sbagliare questa inizializzazione garantisce un esaurimento di classi di
   blocco. Viceversa, un ciclo che invoca spin_lock_init() su tutti i blocchi li
   mapperebbe tutti alla stessa classe di blocco.

   La morale della favola è che dovete sempre inizializzare esplicitamente i
   vostri blocchi.

Qualcuno potrebbe argomentare che il validatore debba permettere il riuso di
classi di blocco. Tuttavia, se siete tentati dall'argomento, prima revisionate
il codice e pensate alla modifiche necessarie, e tenendo a mente che le classi
di blocco da rimuovere probabilmente sono legate al grafo delle dipendenze. Più
facile a dirsi che a farsi.

Ovviamente, se non esaurite le classi di blocco, la prossima cosa da fare è
quella di trovare le classi non funzionanti. Per prima cosa, il seguente comando
ritorna il numero di classi attualmente in uso assieme al valore massimo::

	grep "lock-classes" /proc/lockdep_stats

Questo comando produce il seguente messaggio::

	lock-classes:                          748 [max: 8191]

Se il numero di assegnazioni (748 qui sopra) aumenta continuamente nel tempo,
allora c'è probabilmente un problema da qualche parte. Il seguente comando può
essere utilizzato per identificare le classi di blocchi problematiche::

	grep "BD" /proc/lockdep

Eseguite il comando e salvatene l'output, quindi confrontatelo con l'output di
un'esecuzione successiva per identificare eventuali problemi. Questo stesso
output può anche aiutarti a trovare situazioni in cui l'inizializzazione del
blocco è stata omessa.

Lettura ricorsiva dei blocchi
-----------------------------

Il resto di questo documento vuole dimostrare che certi cicli equivalgono ad una
possibilità di stallo.

Ci sono tre tipi di bloccatori: gli scrittori (bloccatori esclusivi, come
spin_lock() o write_lock()), lettori non ricorsivi (bloccatori condivisi, come
down_read()), e lettori ricorsivi (bloccatori condivisi ricorsivi, come
rcu_read_lock()). D'ora in poi, per questi tipi di bloccatori, useremo la
seguente notazione:

    W o E: per gli scrittori (bloccatori esclusivi) (W dall'inglese per
           *Writer*, ed E per *Exclusive*).

    r: per i lettori non ricorsivi (r dall'inglese per *reader*).

    R: per i lettori ricorsivi (R dall'inglese per *Reader*).

    S: per qualsiasi lettore (non ricorsivi + ricorsivi), dato che entrambe
       sono bloccatori condivisi (S dall'inglese per *Shared*).

    N: per gli scrittori ed i lettori non ricorsivi, dato che entrambe sono
       non ricorsivi.

Ovviamente, N equivale a "r o W" ed S a "r o R".

Come suggerisce il nome, i lettori ricorsivi sono dei bloccatori a cui è
permesso di acquisire la stessa istanza di blocco anche all'interno della
sezione critica di un altro lettore. In altre parole, permette di annidare la
stessa istanza di blocco nelle sezioni critiche dei lettori.

Dall'altro canto, lo stesso comportamento indurrebbe un lettore non ricorsivo ad
auto infliggersi uno stallo.

La differenza fra questi due tipi di lettori esiste perché: quelli ricorsivi
vengono bloccati solo dal trattenimento di un blocco di scrittura, mentre quelli
non ricorsivi possono essere bloccati dall'attesa di un blocco di scrittura.
Consideriamo il seguente esempio::

    TASK A:            TASK B:

    read_lock(X);
                       write_lock(X);
    read_lock_2(X);

L'attività A acquisisce il blocco di lettura X (non importa se di tipo ricorsivo
o meno) usando read_lock(). Quando l'attività B tenterà di acquisire il blocco
X, si fermerà e rimarrà in attesa che venga rilasciato. Ora se read_lock_2() è
un tipo lettore ricorsivo, l'attività A continuerà perché gli scrittori in
attesa non possono bloccare lettori ricorsivi, e non avremo alcuno stallo.
Tuttavia, se read_lock_2() è un lettore non ricorsivo, allora verrà bloccato
dall'attività B e si causerà uno stallo.

Condizioni bloccanti per lettori/scrittori su uno stesso blocco
---------------------------------------------------------------
Essenzialmente ci sono quattro condizioni bloccanti:

1. Uno scrittore blocca un altro scrittore.
2. Un lettore blocca uno scrittore.
3. Uno scrittore blocca sia i lettori ricorsivi che non ricorsivi.
4. Un lettore (ricorsivo o meno) non blocca altri lettori ricorsivi ma potrebbe
   bloccare quelli non ricorsivi (perché potrebbero esistere degli scrittori in
   attesa).

Di seguito le tabella delle condizioni bloccanti, Y (*Yes*) significa che il
tipo in riga blocca quello in colonna, mentre N l'opposto.

    +---+---+---+---+
    |   | W | r | R |
    +---+---+---+---+
    | W | Y | Y | Y |
    +---+---+---+---+
    | r | Y | Y | N |
    +---+---+---+---+
    | R | Y | Y | N |
    +---+---+---+---+

    (W: scrittori, r: lettori non ricorsivi, R: lettori ricorsivi)

Al contrario dei blocchi per lettori non ricorsivi, quelli ricorsivi vengono
trattenuti da chi trattiene il blocco di scrittura piuttosto che da chi ne
attende il rilascio. Per esempio::

	TASK A:			TASK B:

	read_lock(X);

				write_lock(X);

	read_lock(X);

non produce uno stallo per i lettori ricorsivi, in quanto il processo B rimane
in attesta del blocco X, mentre il secondo read_lock() non ha bisogno di
aspettare perché si tratta di un lettore ricorsivo. Tuttavia, se read_lock()
fosse un lettore non ricorsivo, questo codice produrrebbe uno stallo.

Da notare che in funzione dell'operazione di blocco usate per l'acquisizione (in
particolare il valore del parametro 'read' in lock_acquire()), un blocco può
essere di scrittura (blocco esclusivo), di lettura non ricorsivo (blocco
condiviso e non ricorsivo), o di lettura ricorsivo (blocco condiviso e
ricorsivo). In altre parole, per un'istanza di blocco esistono tre tipi di
acquisizione che dipendono dalla funzione di acquisizione usata: esclusiva, di
lettura non ricorsiva, e di lettura ricorsiva.

In breve, chiamiamo "non ricorsivi" blocchi di scrittura e quelli di lettura non
ricorsiva, mentre "ricorsivi" i blocchi di lettura ricorsivi.

I blocchi ricorsivi non si bloccano a vicenda, mentre quelli non ricorsivi sì
(anche in lettura). Un blocco di lettura non ricorsivi può bloccare uno
ricorsivo, e viceversa.

Il seguente esempio mostra uno stallo con blocchi ricorsivi::

	TASK A:			TASK B:

	read_lock(X);
				read_lock(Y);
	write_lock(Y);
				write_lock(X);

Il processo A attende che il processo B esegua read_unlock() so Y, mentre il
processo B attende che A esegua read_unlock() su X.

Tipi di dipendenze e percorsi forti
-----------------------------------
Le dipendenze fra blocchi tracciano l'ordine con cui una coppia di blocchi viene
acquisita, e perché vi sono 3 tipi di bloccatori, allora avremo 9 tipi di
dipendenze. Tuttavia, vi mostreremo che 4 sono sufficienti per individuare gli
stalli.

Per ogni dipendenza fra blocchi avremo::

  L1 -> L2

Questo significa che lockdep ha visto acquisire L1 prima di L2 nello stesso
contesto di esecuzione. Per quanto riguarda l'individuazione degli stalli, ci
interessa sapere se possiamo rimanere bloccati da L2 mentre L1 viene trattenuto.
In altre parole, vogliamo sapere se esiste un bloccatore L3 che viene bloccato
da L1 e un L2 che viene bloccato da L3. Dunque, siamo interessati a (1) quello
che L1 blocca e (2) quello che blocca L2. Di conseguenza, possiamo combinare
lettori ricorsivi e non per L1 (perché bloccano gli stessi tipi) e possiamo
combinare scrittori e lettori non ricorsivi per L2 (perché vengono bloccati
dagli stessi tipi).

Con questa semplificazione, possiamo dedurre che ci sono 4 tipi di rami nel
grafo delle dipendenze di lockdep:

1) -(ER)->:
            dipendenza da scrittore esclusivo a lettore ricorsivo. "X -(ER)-> Y"
            significa X -> Y, dove X è uno scrittore e Y un lettore ricorsivo.

2) -(EN)->:
            dipendenza da scrittore esclusivo a bloccatore non ricorsivo.
            "X -(EN)->" significa X-> Y, dove X è uno scrittore e Y può essere
            o uno scrittore o un lettore non ricorsivo.

3) -(SR)->:
            dipendenza da lettore condiviso a lettore ricorsivo. "X -(SR)->"
            significa X -> Y, dove X è un lettore (ricorsivo o meno) e Y è un
            lettore ricorsivo.

4) -(SN)->:
            dipendenza da lettore condiviso a bloccatore non ricorsivo.
            "X -(SN)-> Y" significa X -> Y , dove X è un lettore (ricorsivo
            o meno) e Y può essere o uno scrittore o un lettore non ricorsivo.

Da notare che presi due blocchi, questi potrebbero avere più dipendenza fra di
loro. Per esempio::

	TASK A:

	read_lock(X);
	write_lock(Y);
	...

	TASK B:

	write_lock(X);
	write_lock(Y);

Nel grafo delle dipendenze avremo sia X -(SN)-> Y che X -(EN)-> Y.

Usiamo -(xN)-> per rappresentare i rami sia per -(EN)-> che -(SN)->, allo stesso
modo -(Ex)->, -(xR)-> e -(Sx)->

Un "percorso" in un grafo è una serie di nodi e degli archi che li congiungono.
Definiamo un percorso "forte", come il percorso che non ha archi (dipendenze) di
tipo -(xR)-> e -(Sx)->. In altre parole, un percorso "forte" è un percorso da un
blocco ad un altro attraverso le varie dipendenze, e se sul percorso abbiamo X
-> Y -> Z (dove X, Y, e Z sono blocchi), e da X a Y si ha una dipendenza -(SR)->
o -(ER)->, allora fra Y e Z non deve esserci una dipendenza -(SN)-> o -(SR)->.

Nella prossima sezione vedremo perché definiamo questo percorso "forte".

Identificazione di stalli da lettura ricorsiva
----------------------------------------------
Ora vogliamo dimostrare altre due cose:

Lemma 1:

Se esiste un percorso chiuso forte (ciclo forte), allora esiste anche una
combinazione di sequenze di blocchi che causa uno stallo. In altre parole,
l'esistenza di un ciclo forte è sufficiente alla scoperta di uno stallo.

Lemma 2:

Se non esiste un percorso chiuso forte (ciclo forte), allora non esiste una
combinazione di sequenze di blocchi che causino uno stallo. In altre parole, i
cicli forti sono necessari alla rilevazione degli stallo.

Con questi due lemmi possiamo facilmente affermare che un percorso chiuso forte
è sia sufficiente che necessario per avere gli stalli, dunque averli equivale
alla possibilità di imbattersi concretamente in uno stallo. Un percorso chiuso
forte significa che può causare stalli, per questo lo definiamo "forte", ma ci
sono anche cicli di dipendenze che non causeranno stalli.

Dimostrazione di sufficienza (lemma 1):

Immaginiamo d'avere un ciclo forte::

    L1 -> L2 ... -> Ln -> L1

Questo significa che abbiamo le seguenti dipendenze::

    L1   -> L2
    L2   -> L3
    ...
    Ln-1 -> Ln
    Ln   -> L1

Ora possiamo costruire una combinazione di sequenze di blocchi che causano lo
stallo.

Per prima cosa facciamo sì che un processo/processore prenda L1 in L1 -> L2, poi
un altro prende L2 in L2 -> L3, e così via. Alla fine, tutti i Lx in Lx -> Lx+1
saranno trattenuti da processi/processori diversi.

Poi visto che abbiamo L1 -> L2, chi trattiene L1 vorrà acquisire L2 in L1 -> L2,
ma prima dovrà attendere che venga rilasciato da chi lo trattiene. Questo perché
L2 è già trattenuto da un altro processo/processore, ed in più L1 -> L2 e L2 ->
L3 non sono -(xR)-> né -(Sx)-> (la definizione di forte). Questo significa che L2
in L1 -> L2 non è un bloccatore non ricorsivo (bloccabile da chiunque), e L2 in
L2 -> L3 non è uno scrittore (che blocca chiunque).

In aggiunta, possiamo trarre una simile conclusione per chi sta trattenendo L2:
deve aspettare che L3 venga rilasciato, e così via. Ora possiamo dimostrare che
chi trattiene Lx deve aspettare che Lx+1 venga rilasciato. Notiamo che Ln+1 è
L1, dunque si è creato un ciclo dal quale non possiamo uscire, quindi si ha uno
stallo.

Dimostrazione della necessità (lemma 2):

Questo lemma equivale a dire che: se siamo in uno scenario di stallo, allora
deve esiste un ciclo forte nel grafo delle dipendenze.

Secondo Wikipedia[1], se c'è uno stallo, allora deve esserci un ciclo di attese,
ovvero ci sono N processi/processori dove P1 aspetta un blocco trattenuto da P2,
e P2 ne aspetta uno trattenuto da P3, ... e Pn attende che il blocco P1 venga
rilasciato. Chiamiamo Lx il blocco che attende Px, quindi P1 aspetta L1 e
trattiene Ln. Quindi avremo Ln -> L1 nel grafo delle dipendenze. Similarmente,
nel grafo delle dipendenze avremo L1 -> L2, L2 -> L3, ..., Ln-1 -> Ln, il che
significa che abbiamo un ciclo::

	Ln -> L1 -> L2 -> ... -> Ln

, ed ora dimostriamo d'avere un ciclo forte.

Per un blocco Lx, il processo Px contribuisce alla dipendenza Lx-1 -> Lx e Px+1
contribuisce a quella Lx -> Lx+1. Visto che Px aspetta che Px+1 rilasci Lx, sarà
impossibile che Lx in Px+1 sia un lettore e che Lx in Px sia un lettore
ricorsivo. Questo perché i lettori (ricorsivi o meno) non bloccano lettori
ricorsivi. Dunque, Lx-1 -> Lx e Lx -> Lx+1 non possono essere una coppia di
-(xR)-> -(Sx)->. Questo è vero per ogni ciclo, dunque, questo è un ciclo forte.

Riferimenti
-----------

[1]: https://it.wikipedia.org/wiki/Stallo_(informatica)

[2]: Shibu, K. (2009). Intro To Embedded Systems (1st ed.). Tata McGraw-Hill
