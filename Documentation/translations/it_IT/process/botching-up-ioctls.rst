.. include:: ../disclaimer-ita.rst

:Original: Documentation/process/botching-up-ioctls.rst

==========================================
(Come evitare di) Raffazzonare delle ioctl
==========================================

Preso da: https://blog.ffwll.ch/2013/11/botching-up-ioctls.html

Scritto da : Daniel Vetter, Copyright © 2013 Intel Corporation

Una cosa che gli sviluppatori del sottosistema grafico del kernel Linux hanno
imparato negli ultimi anni è l'inutilità di cercare di creare un'interfaccia
unificata per gestire la memoria e le unità esecutive di diverse GPU. Dunque,
oggigiorno ogni driver ha il suo insieme di ioctl per allocare memoria ed
inviare dei programmi alla GPU. Il che è va bene dato che non c'è più un insano
sistema che finge di essere generico, ma al suo posto ci sono interfacce
dedicate. Ma al tempo stesso è più facile incasinare le cose.

Per evitare di ripetere gli stessi errori ho preso nota delle lezioni imparate
mentre raffazzonavo il driver drm/i915. La maggior parte di queste lezioni si
focalizzano sui tecnicismi e non sulla visione d'insieme, come le discussioni
riguardo al modo migliore per implementare una ioctl per inviare compiti alla
GPU. Probabilmente, ogni sviluppatore di driver per GPU dovrebbe imparare queste
lezioni in autonomia.


Prerequisiti
------------

Prima i prerequisiti. Seguite i seguenti suggerimenti se non volete fallire in
partenza e ritrovarvi ad aggiungere un livello di compatibilità a 32-bit.

* Usate solamente interi a lunghezza fissa. Per evitare i conflitti coi tipi
  definiti nello spazio utente, il kernel definisce alcuni tipi speciali, come:
  ``__u32``, ``__s64``. Usateli.

* Allineate tutto alla lunghezza naturale delle piattaforma in uso e riempite
  esplicitamente i vuoti. Non necessariamente le piattaforme a 32-bit allineano
  i valori a 64-bit rispettandone l'allineamento, ma le piattaforme a 64-bit lo
  fanno. Dunque, per farlo correttamente in entrambe i casi dobbiamo sempre
  riempire i vuoti.

* Se una struttura dati contiene valori a 64-bit, allora fate si che la sua
  dimensione sia allineata a 64-bit, altrimenti la sua dimensione varierà su
  sistemi a 32-bit e 64-bit. Avere una dimensione differente causa problemi
  quando si passano vettori di strutture dati al kernel, o quando il kernel
  effettua verifiche sulla dimensione (per esempio il sistema drm lo fa).

* I puntatori sono di tipo ``__u64``, con un *cast* da/a ``uintptr_t`` da lato
  spazio utente e da/a ``void __user *`` nello spazio kernel. Sforzatevi il più
  possibile per non ritardare la conversione, o peggio maneggiare ``__u64`` nel
  vostro codice perché questo riduce le verifiche che strumenti come sparse
  possono effettuare. La macro u64_to_user_ptr() può essere usata nel kernel
  per evitare avvisi riguardo interi e puntatori di dimensioni differenti.


Le Basi
-------

Con la gioia d'aver evitato un livello di compatibilità, possiamo ora dare uno
sguardo alle basi. Trascurare questi punti renderà difficile la gestione della
compatibilità all'indietro ed in avanti. E dato che sbagliare al primo colpo è
garantito, dovrete rivisitare il codice o estenderlo per ogni interfaccia.

* Abbiate un modo chiaro per capire dallo spazio utente se una nuova ioctl, o
  l'estensione di una esistente, sia supportata dal kernel in esecuzione. Se non
  potete fidarvi del fatto che un vecchio kernel possa rifiutare correttamente
  un nuovo *flag*, modalità, o ioctl, (probabilmente perché avevate raffazzonato
  qualcosa nel passato) allora dovrete implementare nel driver un meccanismo per
  notificare quali funzionalità sono supportate, o in alternativa un numero di
  versione.

* Abbiate un piano per estendere le ioctl con nuovi *flag* o campi alla fine di
  una struttura dati. Il sistema drm verifica la dimensione di ogni ioctl in
  arrivo, ed estende con zeri ogni incongruenza fra kernel e spazio utente.
  Questo aiuta, ma non è una soluzione completa dato che uno spazio utente nuovo
  su un kernel vecchio non noterebbe che i campi nuovi alla fine della struttura
  vengono ignorati. Dunque, anche questo avrà bisogno di essere notificato dal
  driver allo spazio utente.

* Verificate tutti i campi e *flag* inutilizzati ed i riempimenti siano a 0,
  altrimenti rifiutare la ioctl. Se non lo fate il vostro bel piano per
  estendere le ioctl andrà a rotoli dato che qualcuno userà delle ioctl con
  strutture dati con valori casuali dallo stack nei campi inutilizzati. Il che
  si traduce nell'avere questi campi nell'ABI, e la cui unica utilità sarà
  quella di contenere spazzatura. Per questo dovrete esplicitamente riempire i
  vuoti di tutte le vostre strutture dati, anche se non le userete in un
  vettore. Il riempimento fatto dal compilatore potrebbe contenere valori
  casuali.

* Abbiate un semplice codice di test per ognuno dei casi sopracitati.


Divertirsi coi percorsi d'errore
--------------------------------

Oggigiorno non ci sono più scuse rimaste per permettere ai driver drm di essere
sfruttati per diventare root. Questo significa che dobbiamo avere una completa
validazione degli input e gestire in modo robusto i percorsi - tanto le GPU
moriranno comunque nel più strano dei casi particolari:

 * Le ioctl devono verificare l'overflow dei vettori. Inoltre, per i valori
   interi si devono verificare *overflow*, *underflow*, e *clamping*. Il
   classico esempio è l'inserimento direttamente nell'hardware di valori di
   posizionamento di un'immagine *sprite* quando l'hardware supporta giusto 12
   bit, o qualcosa del genere. Tutto funzionerà finché qualche strano *display
   server* non decide di preoccuparsi lui stesso del *clamping* e il cursore
   farà il giro dello schermo.

 * Avere un test semplice per ogni possibile fallimento della vostra ioctl.
   Verificate che il codice di errore rispetti le aspettative. Ed infine,
   assicuratevi che verifichiate un solo percorso sbagliato per ogni sotto-test
   inviando comunque dati corretti. Senza questo, verifiche precedenti
   potrebbero rigettare la ioctl troppo presto, impedendo l'esecuzione del
   codice che si voleva effettivamente verificare, rischiando quindi di
   mascherare bachi e regressioni.

 * Fate si che tutte le vostre ioctl siano rieseguibili. Prima di tutto X adora
   i segnali; secondo questo vi permetterà di verificare il 90% dei percorsi
   d'errore interrompendo i vostri test con dei segnali. Grazie all'amore di X
   per i segnali, otterrete gratuitamente un eccellente copertura di base per
   tutti i vostri percorsi d'errore. Inoltre, siate consistenti sul modo di
   gestire la riesecuzione delle ioctl - per esempio, drm ha una piccola
   funzione di supporto `drmIoctl` nella sua librerie in spazio utente. Il
   driver i915 l'abbozza con l'ioctl `set_tiling`, ed ora siamo inchiodati per
   sempre con una semantica arcana sia nel kernel che nello spazio utente.


 * Se non potete rendere un pezzo di codice rieseguibile, almeno rendete
   possibile la sua interruzione. Le GPU moriranno e i vostri utenti non vi
   apprezzeranno affatto se tenete in ostaggio il loro scatolotto (mediante un
   processo X insopprimibile). Se anche recuperare lo stato è troppo complicato,
   allora implementate una scadenza oppure come ultima spiaggia una rete di
   sicurezza per rilevare situazioni di stallo quando l'hardware da di matto.

 * Preparate dei test riguardo ai casi particolarmente estremi nel codice di
   recupero del sistema - è troppo facile create uno stallo fra il vostro codice
   anti-stallo e un processo scrittore.


Tempi, attese e mancate scadenze
--------------------------------

Le GPU fanno quasi tutto in modo asincrono, dunque dobbiamo regolare le
operazioni ed attendere quelle in sospeso. Questo è davvero difficile; al
momento nessuna delle ioctl supportante dal driver drm/i915 riesce a farlo
perfettamente, il che significa che qui ci sono ancora una valanga di lezioni da
apprendere.

 * Per fare riferimento al tempo usate sempre ``CLOCK_MONOTONIC``. Oggigiorno
   questo è quello che viene usato di base da alsa, drm, e v4l. Tuttavia,
   lasciate allo spazio utente la possibilità di capire quali *timestamp*
   derivano da domini temporali diversi come il vostro orologio di sistema
   (fornito dal kernel) oppure un contatore hardware indipendente da qualche
   parte. Gli orologi divergeranno, ma con questa informazione gli strumenti di
   analisi delle prestazioni possono compensare il problema. Se il vostro spazio
   utente può ottenere i valori grezzi degli orologi, allora considerate di
   esporre anch'essi.

 * Per descrivere il tempo, usate ``__s64`` per i secondi e ``__u64`` per i
   nanosecondi. Non è il modo migliore per specificare il tempo, ma è
   praticamente uno standard.

 * Verificate che gli input di valori temporali siano normalizzati, e se non lo
   sono scartateli. Fate attenzione perché la struttura dati ``struct ktime``
   del kernel usa interi con segni sia per i secondi che per i nanosecondi.

 * Per le scadenze (*timeout*) usate valori temporali assoluti. Se siete dei
   bravi ragazzi e avete reso la vostra ioctl rieseguibile, allora i tempi
   relativi tendono ad essere troppo grossolani e a causa degli arrotondamenti
   potrebbero estendere in modo indefinito i tempi di attesa ad ogni
   riesecuzione. Particolarmente vero se il vostro orologio di riferimento è
   qualcosa di molto lento come il contatore di *frame*. Con la giacca da
   avvocato delle specifiche diremmo che questo non è un baco perché tutte le
   scadenze potrebbero essere estese - ma sicuramente gli utenti vi odieranno
   quando le animazioni singhiozzano.

 * Considerate l'idea di eliminare tutte le ioctl sincrone con scadenze, e di
   sostituirle con una versione asincrona il cui stato può essere consultato
   attraverso il descrittore di file mediante ``poll``. Questo approccio si
   sposa meglio in un applicazione guidata dagli eventi.

 * Sviluppate dei test per i casi estremi, specialmente verificate che i valori
   di ritorno per gli eventi già completati, le attese terminate con successo, e
   le attese scadute abbiano senso e servano ai vostri scopi.


Non perdere risorse
-------------------
Nel suo piccolo il driver drm implementa un sistema operativo specializzato per
certe GPU. Questo significa che il driver deve esporre verso lo spazio
utente tonnellate di agganci per accedere ad oggetti e altre risorse. Farlo
correttamente porterà con se alcune insidie:

 * Collegate sempre la vita di una risorsa creata dinamicamente, a quella del
   descrittore di file. Considerate una mappatura 1:1 se la vostra risorsa
   dev'essere condivisa fra processi - passarsi descrittori di file sul socket
   unix semplifica la gestione anche per lo spazio utente.

 * Dev'esserci sempre Il supporto ``O_CLOEXEC``.

 * Assicuratevi di avere abbastanza isolamento fra utenti diversi. Di base
   impostate uno spazio dei nomi riservato per ogni descrittore di file, il che
   forzerà ogni condivisione ad essere esplicita. Usate uno spazio più globale
   per dispositivo solo se gli oggetti sono effettivamente unici per quel
   dispositivo. Un controesempio viene dall'interfaccia drm modeset, dove
   oggetti specifici di dispositivo, come i connettori, condividono uno spazio
   dei nomi con oggetti per il *framebuffer*, ma questi non sono per niente
   condivisi. Uno spazio separato, privato di base, per i *framebuffer* sarebbe
   stato meglio.

 * Pensate all'identificazione univoca degli agganci verso lo spazio utente. Per
   esempio, per la maggior parte dei driver drm, si considera fallace la doppia
   sottomissione di un oggetto allo stesso comando ioctl. Ma per evitarlo, se
   gli oggetti sono condivisibili, lo spazio utente ha bisogno di sapere se il
   driver ha importato un oggetto da un altro processo. Non l'ho ancora provato,
   ma considerate l'idea di usare il numero di inode come identificatore per i
   descrittori di file condivisi - che poi è come si distinguono i veri file.
   Sfortunatamente, questo richiederebbe lo sviluppo di un vero e proprio
   filesystem virtuale nel kernel.


Ultimo, ma non meno importante
------------------------------

Non tutti i problemi si risolvono con una nuova ioctl:

* Pensateci su due o tre volte prima di implementare un'interfaccia privata per
  un driver. Ovviamente è molto più veloce seguire questa via piuttosto che
  buttarsi in lunghe discussioni alla ricerca di una soluzione più generica. Ed
  a volte un'interfaccia privata è quello che serve per sviluppare un nuovo
  concetto. Ma alla fine, una volta che c'è un'interfaccia generica a
  disposizione finirete per mantenere due interfacce. Per sempre.

* Considerate interfacce alternative alle ioctl. Gli attributi sysfs sono molto
  meglio per impostazioni che sono specifiche di un dispositivo, o per
  sotto-oggetti con una vita piuttosto statica (come le uscite dei connettori in
  drm con tutti gli attributi per la sovrascrittura delle rilevazioni). O magari
  solo il vostro sistema di test ha bisogno di una certa interfaccia, e allora
  debugfs (che non ha un'interfaccia stabile) sarà la soluzione migliore.

Per concludere. Questo gioco consiste nel fare le cose giuste fin da subito,
dato che se il vostro driver diventa popolare e la piattaforma hardware longeva
finirete per mantenere le vostre ioctl per sempre. Potrete tentare di deprecare
alcune orribili ioctl, ma ci vorranno anni per riuscirci effettivamente. E
ancora, altri anni prima che sparisca l'ultimo utente capace di lamentarsi per
una regressione.
