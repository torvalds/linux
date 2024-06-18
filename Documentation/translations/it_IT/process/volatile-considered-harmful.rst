.. include:: ../disclaimer-ita.rst

:Original: :ref:`Documentation/process/volatile-considered-harmful.rst <volatile_considered_harmful>`
:Translator: Federico Vaga <federico.vaga@vaga.pv.it>

.. _it_volatile_considered_harmful:

Perché la parola chiave "volatile" non dovrebbe essere usata
------------------------------------------------------------

Spesso i programmatori C considerano volatili quelle variabili che potrebbero
essere cambiate al di fuori dal thread di esecuzione corrente; come risultato,
a volte saranno tentati dall'utilizzare *volatile* nel kernel per le
strutture dati condivise.  In altre parole, gli è stato insegnato ad usare
*volatile* come una variabile atomica di facile utilizzo, ma non è così.
L'uso di *volatile* nel kernel non è quasi mai corretto; questo documento ne
descrive le ragioni.

Il punto chiave da capire su *volatile* è che il suo scopo è quello di
sopprimere le ottimizzazioni, che non è quasi mai quello che si vuole.
Nel kernel si devono proteggere le strutture dati condivise contro accessi
concorrenti e indesiderati: questa è un'attività completamente diversa.
Il processo di protezione contro gli accessi concorrenti indesiderati eviterà
anche la maggior parte dei problemi relativi all'ottimizzazione in modo più
efficiente.

Come *volatile*, le primitive del kernel che rendono sicuro l'accesso ai dati
(spinlock, mutex, barriere di sincronizzazione, ecc) sono progettate per
prevenire le ottimizzazioni indesiderate.  Se vengono usate opportunamente,
non ci sarà bisogno di utilizzare *volatile*.  Se vi sembra che *volatile* sia
comunque necessario, ci dev'essere quasi sicuramente un baco da qualche parte.
In un pezzo di codice kernel scritto a dovere, *volatile* può solo servire a
rallentare le cose.

Considerate questo tipico blocco di codice kernel::

    spin_lock(&the_lock);
    do_something_on(&shared_data);
    do_something_else_with(&shared_data);
    spin_unlock(&the_lock);

Se tutto il codice seguisse le regole di sincronizzazione, il valore di un
dato condiviso non potrebbe cambiare inaspettatamente mentre si trattiene un
lock.  Un qualsiasi altro blocco di codice che vorrà usare quel dato rimarrà
in attesa del lock.  Gli spinlock agiscono come barriere di sincronizzazione
- sono stati esplicitamente scritti per agire così - il che significa che gli
accessi al dato condiviso non saranno ottimizzati.  Quindi il compilatore
potrebbe pensare di sapere cosa ci sarà nel dato condiviso ma la chiamata
spin_lock(), che agisce come una barriera di sincronizzazione, gli imporrà di
dimenticarsi tutto ciò che sapeva su di esso.

Se il dato condiviso fosse stato dichiarato come *volatile*, la
sincronizzazione rimarrebbe comunque necessaria.  Ma verrà impedito al
compilatore di ottimizzare gli accessi al dato anche _dentro_ alla sezione
critica, dove sappiamo che in realtà nessun altro può accedervi.  Mentre si
trattiene un lock, il dato condiviso non è *volatile*.  Quando si ha a che
fare con dei dati condivisi, un'opportuna sincronizzazione rende inutile
l'uso di *volatile* - anzi potenzialmente dannoso.

L'uso di *volatile* fu originalmente pensato per l'accesso ai registri di I/O
mappati in memoria.  All'interno del kernel, l'accesso ai registri, dovrebbe
essere protetto dai lock, ma si potrebbe anche desiderare che il compilatore
non "ottimizzi" l'accesso ai registri all'interno di una sezione critica.
Ma, all'interno del kernel, l'accesso alla memoria di I/O viene sempre fatto
attraverso funzioni d'accesso; accedere alla memoria di I/O direttamente
con i puntatori è sconsigliato e non funziona su tutte le architetture.
Queste funzioni d'accesso sono scritte per evitare ottimizzazioni indesiderate,
quindi, di nuovo, *volatile* è inutile.

Un'altra situazione dove qualcuno potrebbe essere tentato dall'uso di
*volatile*, è nel caso in cui il processore è in un'attesa attiva sul valore
di una variabile.  Il modo giusto di fare questo tipo di attesa è il seguente::

    while (my_variable != what_i_want)
        cpu_relax();

La chiamata cpu_relax() può ridurre il consumo di energia del processore
o cedere il passo ad un processore hyperthreaded gemello; funziona anche come
una barriera per il compilatore, quindi, ancora una volta, *volatile* non è
necessario.  Ovviamente, tanto per puntualizzare, le attese attive sono
generalmente un atto antisociale.

Ci sono comunque alcune rare situazioni dove l'uso di *volatile* nel kernel
ha senso:

  - Le funzioni d'accesso sopracitate potrebbero usare *volatile* su quelle
    architetture che supportano l'accesso diretto alla memoria di I/O.
    In pratica, ogni chiamata ad una funzione d'accesso diventa una piccola
    sezione critica a se stante, e garantisce che l'accesso avvenga secondo
    le aspettative del programmatore.

  - I codice *inline assembly* che fa cambiamenti nella memoria, ma che non
    ha altri effetti espliciti, rischia di essere rimosso da GCC.  Aggiungere
    la parola chiave *volatile* a questo codice ne previene la rimozione.

  - La variabile jiffies è speciale in quanto assume un valore diverso ogni
    volta che viene letta ma può essere lette senza alcuna sincronizzazione.
    Quindi jiffies può essere *volatile*, ma l'aggiunta ad altre variabili di
    questo è sconsigliata.  Jiffies è considerata uno "stupido retaggio"
    (parole di Linus) in questo contesto; correggerla non ne varrebbe la pena e
    causerebbe più problemi.

  - I puntatori a delle strutture dati in una memoria coerente che potrebbe
    essere modificata da dispositivi di I/O può, a volte, essere legittimamente
    *volatile*.  Un esempio pratico può essere quello di un adattatore di rete
    che utilizza un puntatore ad un buffer circolare, questo viene cambiato
    dall'adattatore per indicare quali descrittori sono stati processati.

Per la maggior parte del codice, nessuna delle giustificazioni sopracitate può
essere considerata.  Di conseguenza, l'uso di *volatile* è probabile che venga
visto come un baco e porterà a verifiche aggiuntive.  Gli sviluppatori tentati
dall'uso di *volatile* dovrebbero fermarsi e pensare a cosa vogliono davvero
ottenere.

Le modifiche che rimuovono variabili *volatile* sono generalmente ben accette
- purché accompagnate da una giustificazione che dimostri che i problemi di
concorrenza siano stati opportunamente considerati.

Riferimenti
===========

[1] https://lwn.net/Articles/233481/

[2] https://lwn.net/Articles/233482/

Crediti
=======

Impulso e ricerca originale di Randy Dunlap

Scritto da Jonathan Corbet

Migliorato dai commenti di Satyam Sharma, Johannes Stezenbach, Jesper
Juhl, Heikki Orsila, H. Peter Anvin, Philipp Hahn, e Stefan Richter.
