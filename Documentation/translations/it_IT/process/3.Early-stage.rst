.. include:: ../disclaimer-ita.rst

:Original: :ref:`Documentation/process/3.Early-stage.rst <development_early_stage>`
:Translator: Alessia Mantegazza <amantegazza@vaga.pv.it>

.. _it_development_early_stage:

I primi passi della pianificazione
==================================

Osservando un progetto di sviluppo per il kernel Linux, si potrebbe essere
tentati dal saltare tutto e iniziare a codificare.  Tuttavia, come ogni
progetto significativo, molta della preparazione per giungere al successo
viene fatta prima che una sola linea di codice venga scritta.  Il tempo speso
nella pianificazione e la comunicazione può far risparmiare molto
tempo in futuro.

Specificare il problema
-----------------------

Come qualsiasi progetto ingegneristico, un miglioramento del kernel di
successo parte con una chiara descrizione del problema da risolvere.
In alcuni casi, questo passaggio è facile: ad esempio quando un driver è
richiesto per un particolare dispositivo.  In altri casi invece, si
tende a confondere il problema reale con le soluzioni proposte e questo
può portare all'emergere di problemi.

Facciamo un esempio: qualche anno fa, gli sviluppatori che lavoravano con
linux audio cercarono un modo per far girare le applicazioni senza dropouts
o altri artefatti dovuti all'eccessivo ritardo nel sistema.  La soluzione
alla quale giunsero fu un modulo del kernel destinato ad agganciarsi al
framework Linux Security Module (LSM); questo modulo poteva essere
configurato per dare ad una specifica applicazione accesso allo
schedulatore *realtime*.  Tale modulo fu implementato e inviato nella
lista di discussione linux-kernel, dove incontrò subito dei problemi.

Per gli sviluppatori audio, questo modulo di sicurezza era sufficiente a
risolvere il loro problema nell'immediato.  Per l'intera comunità kernel,
invece, era un uso improprio del framework LSM (che non è progettato per
conferire privilegi a processi che altrimenti non avrebbero potuto ottenerli)
e un rischio per la stabilità del sistema.  Le loro soluzioni di punta nel
breve periodo, comportavano un accesso alla schedulazione realtime attraverso
il meccanismo rlimit, e nel lungo periodo un costante lavoro nella riduzione
dei ritardi.

La comunità audio, comunque, non poteva vedere al di là della singola
soluzione che avevano implementato; erano riluttanti ad accettare alternative.
Il conseguente dissenso lasciò in quegli sviluppatori un senso di
disillusione nei confronti dell'intero processo di sviluppo; uno di loro
scrisse questo messaggio:

	Ci sono numerosi sviluppatori del kernel Linux davvero bravi, ma
	rischiano di restare sovrastati da una vasta massa di stolti arroganti.
	Cercare di comunicare le richieste degli utenti a queste persone è
	una perdita di tempo. Loro sono troppo "intelligenti" per stare ad
	ascoltare dei poveri mortali.

	(http://lwn.net/Articles/131776/).

La realtà delle cose fu differente; gli sviluppatori del kernel erano molto
più preoccupati per la stabilità del sistema, per la manutenzione di lungo
periodo e cercavano la giusta soluzione alla problematica esistente con uno
specifico modulo.  La morale della storia è quella di concentrarsi sul
problema - non su di una specifica soluzione- e di discuterne con la comunità
di sviluppo prima di investire tempo nella scrittura del codice.

Quindi, osservando un progetto di sviluppo del kernel, si dovrebbe
rispondere a questa lista di domande:

- Qual'è, precisamente, il problema che dev'essere risolto?

- Chi sono gli utenti coinvolti da tal problema? A quale caso dovrebbe
  essere indirizzata la soluzione?

- In che modo il kernel risulta manchevole nell'indirizzare il problema
  in questione?

Solo dopo ha senso iniziare a considerare le possibili soluzioni.

Prime discussioni
-----------------

Quando si pianifica un progetto di sviluppo per il kernel, sarebbe quanto meno
opportuno discuterne inizialmente con la comunità prima di lanciarsi
nell'implementazione.  Una discussione preliminare può far risparmiare sia
tempo che problemi in svariati modi:

 - Potrebbe essere che il problema sia già stato risolto nel kernel in
   una maniera che non avete ancora compreso.  Il kernel Linux è grande e ha
   una serie di funzionalità e capacità che non sono scontate nell'immediato.
   Non tutte le capacità del kernel sono documentate così bene come ci
   piacerebbe, ed è facile perdersi qualcosa.  Il vostro autore ha assistito
   alla pubblicazione di un driver intero che duplica un altro driver
   esistente di cui il nuovo autore era ignaro.  Il codice che rinnova
   ingranaggi già esistenti non è soltanto dispendioso; non verrà nemmeno
   accettato nel ramo principale del kernel.

 - Potrebbero esserci proposte che non sono considerate accettabili per
   l'integrazione all'interno del ramo principale. È meglio affrontarle
   prima di scrivere il codice.

 - È possibile che altri sviluppatori abbiano pensato al problema; potrebbero
   avere delle idee per soluzioni migliori, e potrebbero voler contribuire
   alla loro creazione.

Anni di esperienza con la comunità di sviluppo del kernel hanno impartito una
chiara lezione: il codice per il kernel che è pensato e sviluppato a porte
chiuse, inevitabilmente, ha problematiche che si rivelano solo quando il
codice viene rilasciato pubblicamente.  Qualche volta tali problemi sono
importanti e richiedono mesi o anni di sforzi prima che il codice possa
raggiungere gli standard richiesti della comunità.
Alcuni esempi possono essere:

 - La rete Devicescape è stata creata e implementata per sistemi
   mono-processore.  Non avrebbe potuto essere inserita nel ramo principale
   fino a che non avesse supportato anche i sistemi multi-processore.
   Riadattare i meccanismi di sincronizzazione e simili è un compito difficile;
   come risultato, l'inserimento di questo codice (ora chiamato mac80211)
   fu rimandato per più di un anno.

 - Il filesystem Reiser4 include una seria di funzionalità che, secondo
   l'opinione degli sviluppatori principali del kernel, avrebbero dovuto
   essere implementate a livello di filesystem virtuale.  Comprende
   anche funzionalità che non sono facilmente implementabili senza esporre
   il sistema al rischio di uno stallo.  La scoperta tardiva di questi
   problemi - e il diniego a risolverne alcuni - ha avuto come conseguenza
   il fatto che Raiser4 resta fuori dal ramo principale del kernel.

 - Il modulo di sicurezza AppArmor utilizzava strutture dati del
   filesystem virtuale interno in modi che sono stati considerati rischiosi e
   inattendibili.  Questi problemi (tra le altre cose) hanno tenuto AppArmor
   fuori dal ramo principale per anni.

Ciascuno di questi casi è stato un travaglio e ha richiesto del lavoro
straordinario, cose che avrebbero potuto essere evitate con alcune
"chiacchierate" preliminari con gli sviluppatori kernel.

Con chi parlare?
----------------

Quando gli sviluppatori hanno deciso di rendere pubblici i propri progetti, la
domanda successiva sarà: da dove partiamo?  La risposta è quella di trovare
la giusta lista di discussione e il giusto manutentore.  Per le liste di
discussione, il miglior approccio è quello di cercare la lista più adatta
nel file MAINTAINERS.  Se esiste una lista di discussione di sottosistema,
è preferibile pubblicare lì piuttosto che sulla lista di discussione generale
del kernel Linux; avrete maggiori probabilità di trovare sviluppatori con
esperienza sul tema, e l'ambiente che troverete potrebbe essere più
incoraggiante.

Trovare manutentori può rivelarsi un po' difficoltoso.  Ancora, il file
MAINTAINERS è il posto giusto da dove iniziare.  Il file potrebbe non essere
sempre aggiornato, inoltre, non tutti i sottosistemi sono rappresentati qui.
Coloro che sono elencati nel file MAINTAINERS potrebbero, in effetti, non
essere le persone che attualmente svolgono quel determinato ruolo.  Quindi,
quando c'è un dubbio su chi contattare, un trucco utile è quello di usare
git (git log in particolare) per vedere chi attualmente è attivo all'interno
del sottosistema interessato.  Controllate chi sta scrivendo le patch,
e chi, se non ci fosse nessuno, sta aggiungendo la propria firma
(Signed-off-by) a quelle patch.  Quelle sono le persone maggiormente
qualificate per aiutarvi con lo sviluppo di nuovo progetto.

Il compito di trovare il giusto manutentore, a volte, è una tale sfida che
ha spinto gli sviluppatori del kernel a scrivere uno script che li aiutasse
in questa ricerca:

::

	.../scripts/get_maintainer.pl

Se questo script viene eseguito con l'opzione "-f" ritornerà il
manutentore(i) attuale per un dato file o cartella.  Se viene passata una
patch sulla linea di comando, lo script elencherà i manutentori che
dovrebbero riceverne una copia.  Ci sono svariate opzioni che regolano
quanto a fondo get_maintainer.pl debba cercare i manutentori;
siate quindi prudenti nell'utilizzare le opzioni più aggressive poiché
potreste finire per includere sviluppatori che non hanno un vero interesse
per il codice che state modificando.

Se tutto ciò dovesse fallire, parlare con Andrew Morton potrebbe essere
un modo efficace per capire chi è il manutentore di un dato pezzo di codice.

Quando pubblicare
-----------------

Se potete, pubblicate i vostri intenti durante le fasi preliminari, sarà
molto utile.  Descrivete il problema da risolvere e ogni piano che è stato
elaborato per l'implementazione.  Ogni informazione fornita può aiutare
la comunità di sviluppo a fornire spunti utili per il progetto.

Un evento che potrebbe risultare scoraggiate e che potrebbe accadere in
questa fase non è il ricevere una risposta ostile, ma, invece, ottenere
una misera o inesistente reazione.  La triste verità è che: (1) gli
sviluppatori del kernel tendono ad essere occupati, (2) ci sono tante persone
con grandi progetti e poco codice (o anche solo la prospettiva di
avere un codice) a cui riferirsi e (3) nessuno è obbligato a revisionare
o a fare osservazioni in merito ad idee pubblicate da altri.  Oltre a
questo, progetti di alto livello spesso nascondono problematiche che si
rivelano solo quando qualcuno cerca di implementarle; per questa ragione
gli sviluppatori kernel preferirebbero vedere il codice.

Quindi, se una richiesta pubblica di commenti riscuote poco successo, non
pensate che ciò significhi che non ci sia interesse nel progetto.
Sfortunatamente, non potete nemmeno assumere che non ci siano problemi con
la vostra idea.  La cosa migliore da fare in questa situazione è quella di
andare avanti e tenere la comunità informata mentre procedete.

Ottenere riscontri ufficiali
----------------------------

Se il vostro lavoro è stato svolto in un ambiente aziendale - come molto
del lavoro fatto su Linux - dovete, ovviamente, avere il permesso dei
dirigenti prima che possiate pubblicare i progetti, o il codice aziendale,
su una lista di discussione pubblica.  La pubblicazione di codice che non
è stato rilascio espressamente con licenza GPL-compatibile può rivelarsi
problematico; prima la dirigenza, e il personale legale, troverà una decisione
sulla pubblicazione di un progetto, meglio sarà per tutte le persone coinvolte.

A questo punto, alcuni lettori potrebbero pensare che il loro lavoro sul
kernel è preposto a supportare un prodotto che non è ancora ufficialmente
riconosciuto.  Rivelare le intenzioni dei propri datori di lavori in una
lista di discussione pubblica potrebbe non essere una soluzione valida.
In questi casi, vale la pena considerare se la segretezza sia necessaria
o meno; spesso non c'è una reale necessità di mantenere chiusi i progetti di
sviluppo.

Detto ciò, ci sono anche casi dove l'azienda legittimamente non può rivelare
le proprie intenzioni in anticipo durante il processo di sviluppo.  Le aziende
che hanno sviluppatori kernel esperti possono scegliere di procedere a
carte coperte partendo dall'assunto che saranno in grado di evitare, o gestire,
in futuro, eventuali problemi d'integrazione. Per le aziende senza questo tipo
di esperti, la migliore opzione è spesso quella di assumere uno sviluppatore
esterno che revisioni i progetti con un accordo di segretezza.
La Linux Foundation applica un programma di NDA creato appositamente per
aiutare le aziende in questa particolare situazione; potrete trovare più
informazioni sul sito:

    http://www.linuxfoundation.org/en/NDA_program

Questa tipologia di revisione è spesso sufficiente per evitare gravi problemi
senza che sia richiesta l'esposizione pubblica del progetto.
