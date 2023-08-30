.. include:: ../disclaimer-ita.rst

:Original: :ref:`Documentation/process/submitting-patches.rst <submittingpatches>`
:Translator: Federico Vaga <federico.vaga@vaga.pv.it>

.. _it_submittingpatches:

Inviare patch: la guida essenziale per vedere il vostro codice nel kernel
=========================================================================

Una persona o un'azienda che volesse inviare una patch al kernel potrebbe
sentirsi scoraggiata dal processo di sottomissione, specialmente quando manca
una certa familiarità col "sistema".  Questo testo è una raccolta di
suggerimenti che aumenteranno significativamente le probabilità di vedere le
vostre patch accettate.

Questo documento contiene un vasto numero di suggerimenti concisi. Per maggiori
dettagli su come funziona il processo di sviluppo del kernel leggete
Documentation/translations/it_IT/process/development-process.rst. Leggete anche
Documentation/translations/it_IT/process/submit-checklist.rst per una lista di
punti da verificare prima di inviare del codice.
Per delle patch relative alle associazioni per Device Tree leggete
Documentation/translations/it_IT/process/submitting-patches.rst.

Questa documentazione assume che sappiate usare ``git`` per preparare le patch.
Se non siete pratici di ``git``, allora è bene che lo impariate;
renderà la vostra vita di sviluppatore del kernel molto più semplice.

I sorgenti di alcuni sottosistemi e manutentori contengono più informazioni
riguardo al loro modo di lavorare ed aspettative. Consultate
:ref:`Documentation/translations/it_IT/process/maintainer-handbooks.rst <it_maintainer_handbooks_main>`

Ottenere i sorgenti attuali
---------------------------

Se non avete un repositorio coi sorgenti del kernel più recenti, allora usate
``git`` per ottenerli.  Vorrete iniziare col repositorio principale che può
essere recuperato col comando::

  git clone git://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git

Notate, comunque, che potreste non voler sviluppare direttamente coi sorgenti
principali del kernel.  La maggior parte dei manutentori hanno i propri
sorgenti e desiderano che le patch siano preparate basandosi su di essi.
Guardate l'elemento **T:** per un determinato sottosistema nel file MAINTANERS
che troverete nei sorgenti, o semplicemente chiedete al manutentore nel caso
in cui i sorgenti da usare non siano elencati il quel file.

.. _it_describe_changes:

Descrivete le vostre modifiche
------------------------------

Descrivete il vostro problema. Esiste sempre un problema che via ha spinto
ha fare il vostro lavoro, che sia la correzione di un baco da una riga o una
nuova funzionalità da 5000 righe di codice.  Convincete i revisori che vale
la pena risolvere il vostro problema e che ha senso continuare a leggere oltre
al primo paragrafo.

Descrivete ciò che sarà visibile agli utenti.  Chiari incidenti nel sistema
e blocchi sono abbastanza convincenti, ma non tutti i bachi sono così evidenti.
Anche se il problema è stato scoperto durante la revisione del codice,
descrivete l'impatto che questo avrà sugli utenti.  Tenete presente che
la maggior parte delle installazioni Linux usa un kernel che arriva dai
sorgenti stabili o dai sorgenti di una distribuzione particolare che prende
singolarmente le patch dai sorgenti principali; quindi, includete tutte
le informazioni che possono essere utili a capire le vostre modifiche:
le circostanze che causano il problema, estratti da dmesg, descrizioni di
un incidente di sistema, prestazioni di una regressione, picchi di latenza,
blocchi, eccetera.

Quantificare le ottimizzazioni e i compromessi.  Se affermate di aver
migliorato le prestazioni, il consumo di memoria, l'impatto sollo stack,
o la dimensione del file binario, includete dei numeri a supporto della
vostra dichiarazione.  Ma ricordatevi di descrivere anche eventuali costi
che non sono ovvi.  Solitamente le ottimizzazioni non sono gratuite, ma sono
un compromesso fra l'uso di CPU, la memoria e la leggibilità; o, quando si
parla di ipotesi euristiche, fra differenti carichi.  Descrivete i lati
negativi che vi aspettate dall'ottimizzazione cosicché i revisori possano
valutare i costi e i benefici.

Una volta che il problema è chiaro, descrivete come lo risolvete andando
nel dettaglio tecnico.  È molto importante che descriviate la modifica
in un inglese semplice cosicché i revisori possano verificare che il codice si
comporti come descritto.

I manutentori vi saranno grati se scrivete la descrizione della patch in un
formato che sia compatibile con il gestore dei sorgenti usato dal kernel,
``git``, come un "commit log". Leggete :ref:`it_the_canonical_patch_format`.

Risolvete solo un problema per patch.  Se la vostra descrizione inizia ad
essere lunga, potrebbe essere un segno che la vostra patch necessita d'essere
divisa. Leggete :ref:`it_split_changes`.

Quando inviate o rinviate una patch o una serie, includete la descrizione
completa delle modifiche e la loro giustificazione.  Non limitatevi a dire che
questa è la versione N della patch (o serie).  Non aspettatevi che i
manutentori di un sottosistema vadano a cercare le versioni precedenti per
cercare la descrizione da aggiungere.  In pratica, la patch (o serie) e la sua
descrizione devono essere un'unica cosa.  Questo aiuta i manutentori e i
revisori.  Probabilmente, alcuni revisori non hanno nemmeno ricevuto o visto
le versioni precedenti della patch.

Descrivete le vostro modifiche usando l'imperativo, per esempio "make xyzzy
do frotz" piuttosto che "[This patch] makes xyzzy do frotz" or "[I] changed
xyzzy to do frotz", come se steste dando ordini al codice di cambiare il suo
comportamento.

Se ci sono delle discussioni, o altre informazioni d'interesse, che fanno
riferimento alla patch, allora aggiungete l'etichetta 'Link:' per farvi
riferimento. Per esempio, se la vostra patch corregge un baco potete aggiungere
quest'etichetta per fare riferimento ad un rapporto su una lista di discussione
o un *bug tracker*. Un altro esempio; potete usare quest'etichetta per far
riferimento ad una discussione precedentemente avvenuta su una lista di
discussione, o qualcosa di documentato sul web, da cui poi è nata la patch in
questione.

Quando volete fare riferimento ad una lista di discussione, preferite il
servizio d'archiviazione lore.kernel.org. Per create un collegamento URL è
sufficiente usare il campo ``Message-Id``, presente nell'intestazione del
messaggio, senza parentesi angolari. Per esempio::

     Link: https://lore.kernel.org/r/30th.anniversary.repost@klaava.Helsinki.FI/

Prima d'inviare il messaggio ricordatevi di verificare che il collegamento così
creato funzioni e che indirizzi verso il messaggio desiderato.

Tuttavia, provate comunque a dare una spiegazione comprensibile anche senza
accedere alle fonti esterne. Inoltre, riassumente i punti più salienti che hanno
condotto all'invio della patch.

Se volete far riferimento a uno specifico commit, non usate solo
l'identificativo SHA-1.  Per cortesia, aggiungete anche la breve riga
riassuntiva del commit per rendere la chiaro ai revisori l'oggetto.
Per esempio::

	Commit e21d2170f36602ae2708 ("video: remove unnecessary
	platform_set_drvdata()") removed the unnecessary
	platform_set_drvdata(), but left the variable "dev" unused,
	delete it.

Dovreste anche assicurarvi di usare almeno i primi 12 caratteri
dell'identificativo SHA-1.  Il repositorio del kernel ha *molti* oggetti e
questo rende possibile la collisione fra due identificativi con pochi
caratteri.  Tenete ben presente che anche se oggi non ci sono collisioni con il
vostro identificativo a 6 caratteri, potrebbero essercene fra 5 anni da oggi.

Se la vostra patch corregge un baco in un commit specifico, per esempio avete
trovato un problema usando ``git bisect``, per favore usate l'etichetta
'Fixes:' indicando i primi 12 caratteri dell'identificativo SHA-1 seguiti
dalla riga riassuntiva.  Per esempio::

	Fixes: e21d2170f366 ("video: remove unnecessary platform_set_drvdata()")

La seguente configurazione di ``git config`` può essere usata per formattare
i risultati dei comandi ``git log`` o ``git show`` come nell'esempio
precedente::

	[core]
		abbrev = 12
	[pretty]
		fixes = Fixes: %h (\"%s\")

Un esempio::

       $ git log -1 --pretty=fixes 54a4f0239f2e
       Fixes: 54a4f0239f2e ("KVM: MMU: make kvm_mmu_zap_page() return the number of pages it actually freed")

.. _it_split_changes:

Separate le vostre modifiche
----------------------------

Separate ogni **cambiamento logico** in patch distinte.

Per esempio, se i vostri cambiamenti per un singolo driver includono
sia delle correzioni di bachi che miglioramenti alle prestazioni,
allora separateli in due o più patch.  Se i vostri cambiamenti includono
un aggiornamento dell'API e un nuovo driver che lo sfrutta, allora separateli
in due patch.

D'altro canto, se fate una singola modifica su più file, raggruppate tutte
queste modifiche in una singola patch.  Dunque, un singolo cambiamento logico
è contenuto in una sola patch.

Il punto da ricordare è che ogni modifica dovrebbe fare delle modifiche
che siano facilmente comprensibili e che possano essere verificate dai revisori.
Ogni patch dovrebbe essere giustificabile di per sé.

Se al fine di ottenere un cambiamento completo una patch dipende da un'altra,
va bene.  Semplicemente scrivete una nota nella descrizione della patch per
farlo presente: **"this patch depends on patch X"**.

Quando dividete i vostri cambiamenti in una serie di patch, prestate
particolare attenzione alla verifica di ogni patch della serie; per ognuna
il kernel deve compilare ed essere eseguito correttamente.  Gli sviluppatori
che usano ``git bisect`` per scovare i problemi potrebbero finire nel mezzo
della vostra serie in un punto qualsiasi; non vi saranno grati se nel mezzo
avete introdotto dei bachi.

Se non potete condensare la vostra serie di patch in una più piccola, allora
pubblicatene una quindicina alla volta e aspettate che vengano revisionate
ed integrate.


4) Verificate lo stile delle vostre modifiche
---------------------------------------------

Controllate che la vostra patch non violi lo stile del codice, maggiori
dettagli sono disponibili in Documentation/translations/it_IT/process/coding-style.rst.
Non farlo porta semplicemente a una perdita di tempo da parte dei revisori e
voi vedrete la vostra patch rifiutata, probabilmente senza nemmeno essere stata
letta.

Un'eccezione importante si ha quando del codice viene spostato da un file
ad un altro -- in questo caso non dovreste modificare il codice spostato
per nessun motivo, almeno non nella patch che lo sposta.  Questo separa
chiaramente l'azione di spostare il codice e il vostro cambiamento.
Questo aiuta enormemente la revisione delle vere differenze e permette agli
strumenti di tenere meglio la traccia della storia del codice.

Prima di inviare una patch, verificatene lo stile usando l'apposito
verificatore (scripts/checkpatch.pl).  Da notare, comunque, che il verificator
di stile dovrebbe essere visto come una guida, non come un sostituto al
giudizio umano.  Se il vostro codice è migliore nonostante una violazione
dello stile, probabilmente è meglio lasciarlo com'è.

Il verificatore ha tre diversi livelli di severità:
 - ERROR: le cose sono molto probabilmente sbagliate
 - WARNING: le cose necessitano d'essere revisionate con attenzione
 - CHECK: le cose necessitano di un pensierino

Dovreste essere in grado di giustificare tutte le eventuali violazioni rimaste
nella vostra patch.


5) Selezionate i destinatari della vostra patch
-----------------------------------------------

Dovreste sempre inviare una copia della patch ai manutentori dei sottosistemi
interessati dalle modifiche; date un'occhiata al file MAINTAINERS e alla storia
delle revisioni per scoprire chi si occupa del codice. Lo script
scripts/get_maintainer.pl può esservi d'aiuto (passategli il percorso alle
vostre patch). Se non riuscite a trovare un manutentore per il sottosistema su
cui state lavorando, allora Andrew Morton (akpm@linux-foundation.org) sarà la
vostra ultima possibilità.

Normalmente, dovreste anche scegliere una lista di discussione a cui inviare la
vostra serie di patch. La lista di discussione linux-kernel@vger.kernel.org
dovrebbe essere usata per inviare tutte le patch, ma il traffico è tale per cui
diversi sviluppatori la trascurano. Guardate nel file MAINTAINERS per trovare la
lista di discussione dedicata ad un sottosistema; probabilmente lì la vostra
patch riceverà molta più attenzione. Tuttavia, per favore, non spammate le liste
di discussione che non sono interessate al vostro lavoro.

Molte delle liste di discussione relative al kernel vengono ospitate su
vger.kernel.org; potete trovare un loro elenco alla pagina
http://vger.kernel.org/vger-lists.html.  Tuttavia, ci sono altre liste di
discussione ospitate altrove.

Non inviate più di 15 patch alla volta sulle liste di discussione vger!!!

L'ultimo giudizio sull'integrazione delle modifiche accettate spetta a
Linux Torvalds.  Il suo indirizzo e-mail è <torvalds@linux-foundation.org>.
Riceve moltissime e-mail, e, a questo punto, solo poche patch passano
direttamente attraverso il suo giudizio; quindi, dovreste fare del vostro
meglio per -evitare di- inviargli e-mail.

Se avete una patch che corregge un baco di sicurezza che potrebbe essere
sfruttato, inviatela a security@kernel.org.  Per bachi importanti, un breve
embargo potrebbe essere preso in considerazione per dare il tempo alle
distribuzioni di prendere la patch e renderla disponibile ai loro utenti;
in questo caso, ovviamente, la patch non dovrebbe essere inviata su alcuna
lista di discussione pubblica. Leggete anche
Documentation/process/security-bugs.rst.

Patch che correggono bachi importanti su un kernel già rilasciato, dovrebbero
essere inviate ai manutentori dei kernel stabili aggiungendo la seguente riga::

  Cc: stable@vger.kernel.org

nella vostra patch, nell'area dedicata alle firme (notate, NON come destinatario
delle e-mail).  In aggiunta a questo file, dovreste leggere anche
Documentation/translations/it_IT/process/stable-kernel-rules.rst.

Se le modifiche hanno effetti sull'interfaccia con lo spazio utente, per favore
inviate una patch per le pagine man ai manutentori di suddette pagine (elencati
nel file MAINTAINERS), o almeno una notifica circa la vostra modifica,
cosicché l'informazione possa trovare la sua strada nel manuale.  Le modifiche
all'API dello spazio utente dovrebbero essere inviate in copia anche a
linux-api@vger.kernel.org.

Niente: MIME, links, compressione, allegati.  Solo puro testo
-------------------------------------------------------------

Linus e gli altri sviluppatori del kernel devono poter commentare
le modifiche che sottomettete.  Per uno sviluppatore è importante
essere in grado di "citare" le vostre modifiche, usando normali
programmi di posta elettronica, cosicché sia possibile commentare
una porzione specifica del vostro codice.

Per questa ragione tutte le patch devono essere inviate via e-mail
come testo. Il modo più facile, e quello raccomandato, è con ``git
send-email``.  Al sito https://git-send-email.io è disponibile una
guida interattiva sull'uso di ``git send-email``.

Se decidete di non usare ``git send-email``:

.. warning::

  Se decidete di copiare ed incollare la patch nel corpo dell'e-mail, state
  attenti che il vostro programma non corrompa il contenuto con andate
  a capo automatiche.

La patch non deve essere un allegato MIME, compresso o meno.  Molti
dei più popolari programmi di posta elettronica non trasmettono un allegato
MIME come puro testo, e questo rende impossibile commentare il vostro codice.
Inoltre, un allegato MIME rende l'attività di Linus più laboriosa, diminuendo
così la possibilità che il vostro allegato-MIME venga accettato.

Eccezione: se il vostro servizio di posta storpia le patch, allora qualcuno
potrebbe chiedervi di rinviarle come allegato MIME.

Leggete Documentation/translations/it_IT/process/email-clients.rst
per dei suggerimenti sulla configurazione del programmi di posta elettronica
per l'invio di patch intatte.

Rispondere ai commenti di revisione
-----------------------------------

In risposta alla vostra email, quasi certamente i revisori vi
invieranno dei commenti su come migliorare la vostra patch.  Dovete
rispondere a questi commenti; ignorare i revisori è un ottimo modo per
essere ignorati.  Riscontri o domande che non conducono ad una
modifica del codice quasi certamente dovrebbero portare ad un commento
nel changelog cosicché il prossimo revisore potrà meglio comprendere
cosa stia accadendo.

Assicuratevi di dire ai revisori quali cambiamenti state facendo e di
ringraziarli per il loro tempo.  Revisionare codice è un lavoro faticoso e che
richiede molto tempo, e a volte i revisori diventano burberi. Tuttavia, anche in
questo caso, rispondete con educazione e concentratevi sul problema che hanno
evidenziato. Quando inviate una versione successiva ricordatevi di aggiungere un
``patch changelog`` alla email di intestazione o ad ogni singola patch spiegando
le differenze rispetto a sottomissioni precedenti (vedere
:ref:`it_the_canonical_patch_format`).

Leggete Documentation/translations/it_IT/process/email-clients.rst per
le raccomandazioni sui programmi di posta elettronica e l'etichetta da usare
sulle liste di discussione.

.. _it_resend_reminders:

Non scoraggiatevi - o impazientitevi
------------------------------------

Dopo che avete inviato le vostre modifiche, siate pazienti e aspettate.
I revisori sono persone occupate e potrebbero non ricevere la vostra patch
immediatamente.

Un tempo, le patch erano solite scomparire nel vuoto senza alcun commento,
ma ora il processo di sviluppo funziona meglio.  Dovreste ricevere commenti
in una settimana o poco più; se questo non dovesse accadere, assicuratevi di
aver inviato le patch correttamente.  Aspettate almeno una settimana prima di
rinviare le modifiche o sollecitare i revisori - probabilmente anche di più
durante la finestra d'integrazione.

Potete anche rinviare la patch, o la serie di patch, dopo un paio di settimane
aggiungendo la parola "RESEND" nel titolo::

    [PATCH Vx RESEND] sub/sys: Condensed patch summary

Ma non aggiungete "RESEND" quando state sottomettendo una versione modificata
della vostra patch, o serie di patch - "RESEND" si applica solo alla
sottomissione di patch, o serie di patch, che non hanno subito modifiche
dall'ultima volta che sono state inviate.

Aggiungete PATCH nell'oggetto
-----------------------------

Dato l'alto volume di e-mail per Linus, e la lista linux-kernel, è prassi
prefiggere il vostro oggetto con [PATCH].  Questo permette a Linus e agli
altri sviluppatori del kernel di distinguere facilmente le patch dalle altre
discussioni.

``git send-email`` lo fa automaticamente.


Firmate il vostro lavoro - Il certificato d'origine dello sviluppatore
----------------------------------------------------------------------

Per migliorare la tracciabilità su "chi ha fatto cosa", specialmente per
quelle patch che per raggiungere lo stadio finale passano attraverso
diversi livelli di manutentori, abbiamo introdotto la procedura di "firma"
delle patch che vengono inviate per e-mail.

La firma è una semplice riga alla fine della descrizione della patch che
certifica che l'avete scritta voi o che avete il diritto di pubblicarla
come patch open-source.  Le regole sono abbastanza semplici: se potete
certificare quanto segue:

Il certificato d'origine dello sviluppatore 1.1
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Contribuendo a questo progetto, io certifico che:

        (a) Il contributo è stato creato interamente, o in parte, da me e che
            ho il diritto di inviarlo in accordo con la licenza open-source
            indicata nel file; oppure

        (b) Il contributo è basato su un lavoro precedente che, nei limiti
            della mia conoscenza, è coperto da un'appropriata licenza
            open-source che mi da il diritto di modificarlo e inviarlo,
            le cui modifiche sono interamente o in parte mie, in accordo con
            la licenza open-source (a meno che non abbia il permesso di usare
            un'altra licenza) indicata nel file; oppure

        (c) Il contributo mi è stato fornito direttamente da qualcuno che
            ha certificato (a), (b) o (c) e non l'ho modificata.

        (d) Capisco e concordo col fatto che questo progetto e i suoi
            contributi sono pubblici e che un registro dei contributi (incluse
            tutte le informazioni personali che invio con essi, inclusa la mia
            firma) verrà mantenuto indefinitamente e che possa essere
            ridistribuito in accordo con questo progetto o le licenze
            open-source coinvolte.

poi dovete solo aggiungere una riga che dice::

	Signed-off-by: Random J Developer <random@developer.example.org>

usando il vostro vero nome (spiacenti, non si accettano
contributi anonimi). Questo verrà fatto automaticamente se usate
``git commit -s``. Anche il ripristino di uno stato precedente dovrebbe
includere "Signed-off-by", se usate ``git revert -s`` questo verrà
fatto automaticamente.

Alcune persone aggiungono delle etichette alla fine.  Per ora queste verranno
ignorate, ma potete farlo per meglio identificare procedure aziendali interne o
per aggiungere dettagli circa la firma.

In seguito al SoB (Signed-off-by:) dell'autore ve ne sono altri da
parte di tutte quelle persone che si sono occupate della gestione e
del trasporto della patch. Queste però non sono state coinvolte nello
sviluppo, ma la loro sequenza d'apparizione ci racconta il percorso
**reale** che una patch a intrapreso dallo sviluppatore, fino al
manutentore, per poi giungere a Linus.


Quando utilizzare Acked-by:, Cc:, e Co-developed-by:
----------------------------------------------------

L'etichetta Signed-off-by: indica che il firmatario è stato coinvolto nello
sviluppo della patch, o che era nel suo percorso di consegna.

Se una persona non è direttamente coinvolta con la preparazione o gestione
della patch ma desidera firmare e mettere agli atti la loro approvazione,
allora queste persone possono chiedere di aggiungere al changelog della patch
una riga Acked-by:.

Acked-by: viene spesso utilizzato dai manutentori del sottosistema in oggetto
quando quello stesso manutentore non ha contribuito né trasmesso la patch.

Acked-by: non è formale come Signed-off-by:.  Questo indica che la persona ha
revisionato la patch e l'ha trovata accettabile.  Per cui, a volte, chi
integra le patch convertirà un "sì, mi sembra che vada bene" in un Acked-by:
(ma tenete presente che solitamente è meglio chiedere esplicitamente).

Acked-by: non indica l'accettazione di un'intera patch.  Per esempio, quando
una patch ha effetti su diversi sottosistemi e ha un Acked-by: da un
manutentore di uno di questi, significa che il manutentore accetta quella
parte di codice relativa al sottosistema che mantiene.  Qui dovremmo essere
giudiziosi.  Quando si hanno dei dubbi si dovrebbe far riferimento alla
discussione originale negli archivi della lista di discussione.

Se una persona ha avuto l'opportunità di commentare la patch, ma non lo ha
fatto, potete aggiungere l'etichetta ``Cc:`` alla patch.  Questa è l'unica
etichetta che può essere aggiunta senza che la persona in questione faccia
alcunché - ma dovrebbe indicare che la persona ha ricevuto una copia della
patch.  Questa etichetta documenta che terzi potenzialmente interessati sono
stati inclusi nella discussione.

Co-developed-by: indica che la patch è stata cosviluppata da diversi
sviluppatori; viene usato per assegnare più autori (in aggiunta a quello
associato all'etichetta From:) quando più persone lavorano ad una patch.  Dato
che Co-developed-by: implica la paternità della patch, ogni Co-developed-by:
dev'essere seguito immediatamente dal Signed-off-by: del corrispondente
coautore. Qui si applica la procedura di base per sign-off, in pratica
l'ordine delle etichette Signed-off-by: dovrebbe riflettere il più possibile
l'ordine cronologico della storia della patch, indipendentemente dal fatto che
la paternità venga assegnata via From: o Co-developed-by:. Da notare che
l'ultimo Signed-off-by: dev'essere quello di colui che ha sottomesso la patch.

Notate anche che l'etichetta From: è opzionale quando l'autore in From: è
anche la persona (e indirizzo email) indicato nel From: dell'intestazione
dell'email.

Esempio di una patch sottomessa dall'autore in From:::

	<changelog>

	Co-developed-by: First Co-Author <first@coauthor.example.org>
	Signed-off-by: First Co-Author <first@coauthor.example.org>
	Co-developed-by: Second Co-Author <second@coauthor.example.org>
	Signed-off-by: Second Co-Author <second@coauthor.example.org>
	Signed-off-by: From Author <from@author.example.org>

Esempio di una patch sottomessa dall'autore Co-developed-by:::

	From: From Author <from@author.example.org>

	<changelog>

	Co-developed-by: Random Co-Author <random@coauthor.example.org>
	Signed-off-by: Random Co-Author <random@coauthor.example.org>
	Signed-off-by: From Author <from@author.example.org>
	Co-developed-by: Submitting Co-Author <sub@coauthor.example.org>
	Signed-off-by: Submitting Co-Author <sub@coauthor.example.org>

Utilizzare Reported-by:, Tested-by:, Reviewed-by:, Suggested-by: e Fixes:
-------------------------------------------------------------------------

L'etichetta Reported-by da credito alle persone che trovano e riportano i bachi
e si spera che questo possa ispirarli ad aiutarci nuovamente in futuro.
Rammentate che se il baco è stato riportato in privato, dovrete chiedere il
permesso prima di poter utilizzare l'etichetta Reported-by. Questa etichetta va
usata per i bachi, dunque non usatela per richieste di nuove funzionalità.

L'etichetta Tested-by: indica che la patch è stata verificata con successo
(su un qualche sistema) dalla persona citata.  Questa etichetta informa i
manutentori che qualche verifica è stata fatta, fornisce un mezzo per trovare
persone che possano verificare il codice in futuro, e garantisce che queste
stesse persone ricevano credito per il loro lavoro.

Reviewed-by:, invece, indica che la patch è stata revisionata ed è stata
considerata accettabile in accordo con la dichiarazione dei revisori:

Dichiarazione di svista dei revisori
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Offrendo la mia etichetta Reviewed-by, dichiaro quanto segue:

	 (a) Ho effettuato una revisione tecnica di questa patch per valutarne
	     l'adeguatezza ai fini dell'inclusione nel ramo principale del
	     kernel.

	 (b) Tutti i problemi e le domande riguardanti la patch sono stati
	     comunicati al mittente.  Sono soddisfatto dalle risposte
	     del mittente.

	 (c) Nonostante ci potrebbero essere cose migliorabili in queste
	     sottomissione, credo che sia, in questo momento, (1) una modifica
	     di interesse per il kernel, e (2) libera da problemi che
	     potrebbero metterne in discussione l'integrazione.

	 (d) Nonostante abbia revisionato la patch e creda che vada bene,
	     non garantisco (se non specificato altrimenti) che questa
	     otterrà quello che promette o funzionerà correttamente in tutte
	     le possibili situazioni.

L'etichetta Reviewed-by è la dichiarazione di un parere sulla bontà di
una modifica che si ritiene appropriata e senza alcun problema tecnico
importante.  Qualsiasi revisore interessato (quelli che lo hanno fatto)
possono offrire il proprio Reviewed-by per la patch.  Questa etichetta serve
a dare credito ai revisori e a informare i manutentori sul livello di revisione
che è stato fatto sulla patch.  L'etichetta Reviewed-by, quando fornita da
revisori conosciuti per la loro conoscenza sulla materia in oggetto e per la
loro serietà nella revisione, accrescerà le probabilità che la vostra patch
venga integrate nel kernel.

Quando si riceve una email sulla lista di discussione da un tester o
un revisore, le etichette Tested-by o Reviewed-by devono essere
aggiunte dall'autore quando invierà nuovamente la patch. Tuttavia, se
la patch è cambiata in modo significativo, queste etichette potrebbero
non avere più senso e quindi andrebbero rimosse. Solitamente si tiene traccia
della rimozione nel changelog della patch (subito dopo il separatore '---').

L'etichetta Suggested-by: indica che l'idea della patch è stata suggerita
dalla persona nominata e le da credito. Tenete a mente che questa etichetta
non dovrebbe essere aggiunta senza un permesso esplicito, specialmente se
l'idea non è stata pubblicata in un forum pubblico.  Detto ciò, dando credito
a chi ci fornisce delle idee, si spera di poterli ispirare ad aiutarci
nuovamente in futuro.

L'etichetta Fixes: indica che la patch corregge un problema in un commit
precedente.  Serve a chiarire l'origine di un baco, il che aiuta la revisione
del baco stesso.  Questa etichetta è di aiuto anche per i manutentori dei
kernel stabili al fine di capire quale kernel deve ricevere la correzione.
Questo è il modo suggerito per indicare che un baco è stato corretto nella
patch. Per maggiori dettagli leggete :ref:`it_describe_changes`

Da notare che aggiungere un tag "Fixes:" non esime dalle regole
previste per i kernel stabili, e nemmeno dalla necessità di aggiungere
in copia conoscenza stable@vger.kernel.org su tutte le patch per
suddetti kernel.

.. _it_the_canonical_patch_format:

Il formato canonico delle patch
-------------------------------

Questa sezione descrive il formato che dovrebbe essere usato per le patch.
Notate che se state usando un repositorio ``git`` per salvare le vostre patch
potere usare il comando ``git format-patch`` per ottenere patch nel formato
appropriato.  Lo strumento non crea il testo necessario, per cui, leggete
le seguenti istruzioni.

L'oggetto di una patch canonica è la riga::

    Subject: [PATCH 001/123] subsystem: summary phrase

Il corpo di una patch canonica contiene i seguenti elementi:

  - Una riga ``from`` che specifica l'autore della patch, seguita
    da una riga vuota (necessaria soltanto se la persona che invia la
    patch non ne è l'autore).

  - Il corpo della spiegazione, con linee non più lunghe di 75 caratteri,
    che verrà copiato permanentemente nel changelog per descrivere la patch.

  - Una riga vuota

  - Le righe ``Signed-off-by:``, descritte in precedenza, che finiranno
    anch'esse nel changelog.

  - Una linea di demarcazione contenente semplicemente ``---``.

  - Qualsiasi altro commento che non deve finire nel changelog.

  - Le effettive modifiche al codice (il prodotto di ``diff``).

Il formato usato per l'oggetto permette ai programmi di posta di usarlo
per ordinare le patch alfabeticamente - tutti i programmi di posta hanno
questa funzionalità - dato che al numero sequenziale si antepongono degli zeri;
in questo modo l'ordine numerico ed alfabetico coincidono.

Il ``subsystem`` nell'oggetto dell'email dovrebbe identificare l'area
o il sottosistema modificato dalla patch.

La ``summary phrase`` nell'oggetto dell'email dovrebbe descrivere brevemente
il contenuto della patch.  La ``summary phrase`` non dovrebbe essere un nome
di file. Non utilizzate la stessa ``summary phrase`` per tutte le patch in
una serie (dove una ``serie di patch`` è una sequenza ordinata di diverse
patch correlate).

Ricordatevi che la ``summary phrase`` della vostra email diventerà un
identificatore globale ed unico per quella patch.  Si propaga fino al
changelog ``git``.  La ``summary phrase`` potrà essere usata in futuro
dagli sviluppatori per riferirsi a quella patch.  Le persone vorranno
cercare la ``summary phrase`` su internet per leggere le discussioni che la
riguardano.  Potrebbe anche essere l'unica cosa che le persone vedranno
quando, in due o tre mesi, riguarderanno centinaia di patch usando strumenti
come ``gitk`` o ``git log --oneline``.

Per queste ragioni, dovrebbe essere lunga fra i 70 e i 75 caratteri, e deve
descrivere sia cosa viene modificato, sia il perché sia necessario. Essere
brevi e descrittivi è una bella sfida, ma questo è quello che fa un riassunto
ben scritto.

La ``summary phrase`` può avere un'etichetta (*tag*) di prefisso racchiusa fra
le parentesi quadre "Subject: [PATCH <tag>...] <summary phrase>".
Le etichette non verranno considerate come parte della frase riassuntiva, ma
indicano come la patch dovrebbe essere trattata.  Fra le etichette più comuni
ci sono quelle di versione che vengono usate quando una patch è stata inviata
più volte (per esempio, "v1, v2, v3"); oppure "RFC" per indicare che si
attendono dei commenti (*Request For Comments*).

Se ci sono quattro patch nella serie, queste dovrebbero essere
enumerate così: 1/4, 2/4, 3/4, 4/4.  Questo assicura che gli
sviluppatori capiranno l'ordine in cui le patch dovrebbero essere
applicate, e per tracciare quelle che hanno revisionate o che hanno
applicato.

Un paio di esempi di oggetti::

    Subject: [PATCH 2/5] ext2: improve scalability of bitmap searching
    Subject: [PATCH v2 01/27] x86: fix eflags tracking
    Subject: [PATCH v2] sub/sys: Condensed patch summary
    Subject: [PATCH v2 M/N] sub/sys: Condensed patch summary

La riga ``from`` dev'essere la prima nel corpo del messaggio ed è nel
formato:

        From: Patch Author <author@example.com>

La riga ``from`` indica chi verrà accreditato nel changelog permanente come
l'autore della patch.  Se la riga ``from`` è mancante, allora per determinare
l'autore da inserire nel changelog verrà usata la riga ``From``
nell'intestazione dell'email.

Il corpo della spiegazione verrà incluso nel changelog permanente, per cui
deve aver senso per un lettore esperto che è ha dimenticato i dettagli della
discussione che hanno portato alla patch.  L'inclusione di informazioni
sui problemi oggetto dalla patch (messaggi del kernel, messaggi di oops,
eccetera) è particolarmente utile per le persone che potrebbero cercare fra
i messaggi di log per la patch che li tratta. Il testo dovrebbe essere scritto
con abbastanza dettagli da far capire al lettore **perché** quella
patch fu creata, e questo a distanza di settimane, mesi, o addirittura
anni.

Se la patch corregge un errore di compilazione, non sarà necessario
includere proprio _tutto_ quello che è uscito dal compilatore;
aggiungete solo quello che è necessario per far si che la vostra patch
venga trovata.  Come nella ``summary phrase``, è importante essere sia
brevi che descrittivi.

La linea di demarcazione ``---`` serve essenzialmente a segnare dove finisce
il messaggio di changelog.

Aggiungere il ``diffstat`` dopo ``---`` è un buon uso di questo spazio, per
mostrare i file che sono cambiati, e il numero di file aggiunto o rimossi.
Un ``diffstat`` è particolarmente utile per le patch grandi. Se
includete un ``diffstat`` dopo ``---``, usate le opzioni ``-p 1 -w70``
cosicché i nomi dei file elencati non occupino troppo spazio
(facilmente rientreranno negli 80 caratteri, magari con qualche
indentazione).  (``git`` genera di base dei diffstat adatti).

I commenti che sono importanti solo per i manutentori, quindi
inadatti al changelog permanente, dovrebbero essere messi qui.  Un
buon esempio per questo tipo di commenti potrebbe essere il cosiddetto
``patch changelogs`` che descrivere le differenze fra le versioni
della patch.

Queste informazioni devono andare **dopo** la linea ``---`` che separa
il *changelog* dal resto della patch. Le informazioni riguardanti la
versione di una patch non sono parte del *chagelog* che viene incluso
in git. Queste sono informazioni utili solo ai revisori. Se venissero
messe sopra la riga, qualcuno dovrà fare del lavoro manuale per
rimuoverle; cosa che invece viene fatta automaticamente quando vengono
messe correttamente oltre la riga.::

  <commit message>
  ...
  Signed-off-by: Author <author@mail>
  ---
  V2 -> V3: Removed redundant helper function
  V1 -> V2: Cleaned up coding style and addressed review comments

  path/to/file | 5+++--
  ...

Maggiori dettagli sul formato delle patch nei riferimenti qui di seguito.

.. _it_backtraces:

Aggiungere i *backtrace* nei messaggi di commit
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

I *backtrace* aiutano a documentare la sequenza di chiamate a funzione
che portano ad un problema. Tuttavia, non tutti i *backtrace* sono
davvero utili. Per esempio, le sequenze iniziali di avvio sono uniche
e ovvie. Copiare integralmente l'output di ``dmesg`` aggiunge tante
informazioni che distraggono dal vero problema (per esempio, i
marcatori temporali, la lista dei moduli, la lista dei registri, lo
stato dello stack).

Quindi, per rendere utile un *backtrace* dovreste eliminare le
informazioni inutili, cosicché ci si possa focalizzare sul
problema. Ecco un esempio di un *backtrace* essenziale::

  unchecked MSR access error: WRMSR to 0xd51 (tried to write 0x0000000000000064)
  at rIP: 0xffffffffae059994 (native_write_msr+0x4/0x20)
  Call Trace:
  mba_wrmsr
  update_domains
  rdtgroup_mkdir

.. _it_explicit_in_reply_to:

Usare esplicitamente In-Reply-To nell'intestazione
--------------------------------------------------

Aggiungere manualmente In-Reply-To: nell'intestazione dell'e-mail
potrebbe essere d'aiuto per associare una patch ad una discussione
precedente, per esempio per collegare la correzione di un baco con l'e-mail
che lo riportava.  Tuttavia, per serie di patch multiple è generalmente
sconsigliato l'uso di In-Reply-To: per collegare precedenti versioni.
In questo modo versioni multiple di una patch non diventeranno un'ingestibile
giungla di riferimenti all'interno dei programmi di posta.  Se un collegamento
è utile, potete usare https://lore.kernel.org/ per ottenere i collegamenti
ad una versione precedente di una serie di patch (per esempio, potete usarlo
per l'email introduttiva alla serie).

Riferimenti
-----------

Andrew Morton, "La patch perfetta" (tpp).
  <https://www.ozlabs.org/~akpm/stuff/tpp.txt>

Jeff Garzik, "Formato per la sottomissione di patch per il kernel Linux"
  <https://web.archive.org/web/20180829112450/http://linux.yyz.us/patch-format.html>

Greg Kroah-Hartman, "Come scocciare un manutentore di un sottosistema"
  <http://www.kroah.com/log/linux/maintainer.html>

  <http://www.kroah.com/log/linux/maintainer-02.html>

  <http://www.kroah.com/log/linux/maintainer-03.html>

  <http://www.kroah.com/log/linux/maintainer-04.html>

  <http://www.kroah.com/log/linux/maintainer-05.html>

  <http://www.kroah.com/log/linux/maintainer-06.html>

No!!!! Basta gigantesche bombe patch alle persone sulla lista linux-kernel@vger.kernel.org!
  <https://lore.kernel.org/r/20050711.125305.08322243.davem@davemloft.net>

Kernel Documentation/translations/it_IT/process/coding-style.rst.

E-mail di Linus Torvalds sul formato canonico di una patch:
  <https://lore.kernel.org/r/Pine.LNX.4.58.0504071023190.28951@ppc970.osdl.org>

Andi Kleen, "Su come sottomettere patch del kernel"
  Alcune strategie su come sottomettere modifiche toste o controverse.

  http://halobates.de/on-submitting-patches.pdf
