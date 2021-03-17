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

Questo documento contiene un vasto numero di suggerimenti concisi.  Per
maggiori dettagli su come funziona il processo di sviluppo del kernel leggete
:ref:`Documentation/translations/it_IT/process <it_development_process_main>`.
Leggete anche :ref:`Documentation/translations/it_IT/process/submit-checklist.rst <it_submitchecklist>`
per una lista di punti da verificare prima di inviare del codice.  Se state
inviando un driver, allora leggete anche :ref:`Documentation/translations/it_IT/process/submitting-drivers.rst <it_submittingdrivers>`;
per delle patch relative alle associazioni per Device Tree leggete
Documentation/devicetree/bindings/submitting-patches.rst.

Molti di questi passi descrivono il comportamento di base del sistema di
controllo di versione ``git``; se utilizzate ``git`` per preparare le vostre
patch molto del lavoro più ripetitivo lo troverete già fatto per voi, tuttavia
dovete preparare e documentare un certo numero di patch.  Generalmente, l'uso
di ``git`` renderà la vostra vita di sviluppatore del kernel più facile.

0) Ottenere i sorgenti attuali
------------------------------

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

Esiste ancora la possibilità di scaricare un rilascio del kernel come archivio
tar (come descritto in una delle prossime sezioni), ma questa è la via più
complicata per sviluppare per il kernel.

1) ``diff -up``
---------------

Se dovete produrre le vostre patch a mano, usate ``diff -up`` o ``diff -uprN``
per crearle.  Git produce di base le patch in questo formato; se state
usando ``git``, potete saltare interamente questa sezione.

Tutte le modifiche al kernel Linux avvengono mediate patch, come descritte
in :manpage:`diff(1)`.  Quando create la vostra patch, assicuratevi di
crearla nel formato "unified diff", come l'argomento ``-u`` di
:manpage:`diff(1)`.
Inoltre, per favore usate l'argomento ``-p`` per mostrare la funzione C
alla quale si riferiscono le diverse modifiche - questo rende il risultato
di ``diff`` molto più facile da leggere.  Le patch dovrebbero essere basate
sulla radice dei sorgenti del kernel, e non sulle sue sottocartelle.

Per creare una patch per un singolo file, spesso è sufficiente fare::

	SRCTREE=linux
	MYFILE=drivers/net/mydriver.c

	cd $SRCTREE
	cp $MYFILE $MYFILE.orig
	vi $MYFILE	# make your change
	cd ..
	diff -up $SRCTREE/$MYFILE{.orig,} > /tmp/patch

Per creare una patch per molteplici file, dovreste spacchettare i sorgenti
"vergini", o comunque non modificati, e fare un ``diff`` coi vostri.
Per esempio::

	MYSRC=/devel/linux

	tar xvfz linux-3.19.tar.gz
	mv linux-3.19 linux-3.19-vanilla
	diff -uprN -X linux-3.19-vanilla/Documentation/dontdiff \
		linux-3.19-vanilla $MYSRC > /tmp/patch

``dontdiff`` è una lista di file che sono generati durante il processo di
compilazione del kernel; questi dovrebbero essere ignorati in qualsiasi
patch generata con :manpage:`diff(1)`.

Assicuratevi che la vostra patch non includa file che non ne fanno veramente
parte.  Al fine di verificarne la correttezza, assicuratevi anche di
revisionare la vostra patch -dopo- averla generata con :manpage:`diff(1)`.

Se le vostre modifiche producono molte differenze, allora dovrete dividerle
in patch indipendenti che modificano le cose in passi logici;  leggete
:ref:`split_changes`.  Questo faciliterà la revisione da parte degli altri
sviluppatori, il che è molto importante se volete che la patch venga accettata.

Se state utilizzando ``git``, ``git rebase -i`` può aiutarvi nel procedimento.
Se non usate ``git``, un'alternativa popolare è ``quilt``
<http://savannah.nongnu.org/projects/quilt>.

.. _it_describe_changes:

2) Descrivete le vostre modifiche
---------------------------------

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
``git``, come un "commit log".  Leggete :ref:`it_explicit_in_reply_to`.

Risolvete solo un problema per patch.  Se la vostra descrizione inizia ad
essere lunga, potrebbe essere un segno che la vostra patch necessita d'essere
divisa. Leggete :ref:`split_changes`.

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

Se la patch corregge un baco conosciuto, fare riferimento a quel baco inserendo
il suo numero o il suo URL.  Se la patch è la conseguenza di una discussione
su una lista di discussione, allora fornite l'URL all'archivio di quella
discussione;  usate i collegamenti a https://lkml.kernel.org/ con il
``Message-Id``, in questo modo vi assicurerete che il collegamento non diventi
invalido nel tempo.

Tuttavia, cercate di rendere la vostra spiegazione comprensibile anche senza
far riferimento a fonti esterne.  In aggiunta ai collegamenti a bachi e liste
di discussione, riassumente i punti più importanti della discussione che hanno
portato alla creazione della patch.

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

.. _it_split_changes:

3) Separate le vostre modifiche
-------------------------------

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
dettagli sono disponibili in :ref:`Documentation/translations/it_IT/process/coding-style.rst <it_codingstyle>`.
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
delle revisioni per scoprire chi si occupa del codice.  Lo script
scripts/get_maintainer.pl può esservi d'aiuto.  Se non riuscite a trovare un
manutentore per il sottosistema su cui state lavorando, allora Andrew Morton
(akpm@linux-foundation.org) sarà la vostra ultima possibilità.

Normalmente, dovreste anche scegliere una lista di discussione a cui inviare
la vostra serie di patch.  La lista di discussione linux-kernel@vger.kernel.org
è proprio l'ultima spiaggia, il volume di email su questa lista fa si che
diversi sviluppatori non la seguano.  Guardate nel file MAINTAINERS per trovare
la lista di discussione dedicata ad un sottosistema; probabilmente lì la vostra
patch riceverà molta più attenzione.  Tuttavia, per favore, non spammate le
liste di discussione che non sono interessate al vostro lavoro.

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
lista di discussione pubblica.

Patch che correggono bachi importanti su un kernel già rilasciato, dovrebbero
essere inviate ai manutentori dei kernel stabili aggiungendo la seguente riga::

  Cc: stable@vger.kernel.org

nella vostra patch, nell'area dedicata alle firme (notate, NON come destinatario
delle e-mail).  In aggiunta a questo file, dovreste leggere anche
:ref:`Documentation/translations/it_IT/process/stable-kernel-rules.rst <it_stable_kernel_rules>`

Tuttavia, notate, che alcuni manutentori di sottosistema preferiscono avere
l'ultima parola su quali patch dovrebbero essere aggiunte ai kernel stabili.
La rete di manutentori, in particolare, non vorrebbe vedere i singoli
sviluppatori aggiungere alle loro patch delle righe come quella sopracitata.

Se le modifiche hanno effetti sull'interfaccia con lo spazio utente, per favore
inviate una patch per le pagine man ai manutentori di suddette pagine (elencati
nel file MAINTAINERS), o almeno una notifica circa la vostra modifica,
cosicché l'informazione possa trovare la sua strada nel manuale.  Le modifiche
all'API dello spazio utente dovrebbero essere inviate in copia anche a
linux-api@vger.kernel.org.

Per le piccole patch potreste aggiungere in CC l'indirizzo
*Trivial Patch Monkey trivial@kernel.org* che ha lo scopo di raccogliere
le patch "banali".  Date uno sguardo al file MAINTAINERS per vedere chi
è l'attuale amministratore.

Le patch banali devono rientrare in una delle seguenti categorie:

- errori grammaticali nella documentazione
- errori grammaticali negli errori che potrebbero rompere :manpage:`grep(1)`
- correzione di avvisi di compilazione (riempirsi di avvisi inutili è negativo)
- correzione di errori di compilazione (solo se correggono qualcosa sul serio)
- rimozione di funzioni/macro deprecate
- sostituzione di codice non potabile con uno portabile (anche in codice
  specifico per un'architettura, dato che le persone copiano, fintanto che
  la modifica sia banale)
- qualsiasi modifica dell'autore/manutentore di un file (in pratica
  "patch monkey" in modalità ritrasmissione)


6) Niente: MIME, links, compressione, allegati.  Solo puro testo
----------------------------------------------------------------

Linus e gli altri sviluppatori del kernel devono poter commentare
le modifiche che sottomettete.  Per uno sviluppatore è importante
essere in grado di "citare" le vostre modifiche, usando normali
programmi di posta elettronica, cosicché sia possibile commentare
una porzione specifica del vostro codice.

Per questa ragione tutte le patch devono essere inviate via e-mail
come testo.

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

Leggete :ref:`Documentation/translations/it_IT/process/email-clients.rst <it_email_clients>`
per dei suggerimenti sulla configurazione del programmi di posta elettronica
per l'invio di patch intatte.

7) Dimensione delle e-mail
--------------------------

Le grosse modifiche non sono adatte ad una lista di discussione, e nemmeno
per alcuni manutentori.  Se la vostra patch, non compressa, eccede i 300 kB
di spazio, allora caricatela in una spazio accessibile su internet fornendo
l'URL (collegamento) ad essa.  Ma notate che se la vostra patch eccede i 300 kB
è quasi certo che necessiti comunque di essere spezzettata.

8) Rispondere ai commenti di revisione
--------------------------------------

Quasi certamente i revisori vi invieranno dei commenti su come migliorare
la vostra patch.  Dovete rispondere a questi commenti; ignorare i revisori
è un ottimo modo per essere ignorati.  Riscontri o domande che non conducono
ad una modifica del codice quasi certamente dovrebbero portare ad un commento
nel changelog cosicché il prossimo revisore potrà meglio comprendere cosa stia
accadendo.

Assicuratevi di dire ai revisori quali cambiamenti state facendo e di
ringraziarli per il loro tempo.  Revisionare codice è un lavoro faticoso e che
richiede molto tempo, e a volte i revisori diventano burberi.  Tuttavia, anche
in questo caso, rispondete con educazione e concentratevi sul problema che
hanno evidenziato.

9) Non scoraggiatevi - o impazientitevi
---------------------------------------

Dopo che avete inviato le vostre modifiche, siate pazienti e aspettate.
I revisori sono persone occupate e potrebbero non ricevere la vostra patch
immediatamente.

Un tempo, le patch erano solite scomparire nel vuoto senza alcun commento,
ma ora il processo di sviluppo funziona meglio.  Dovreste ricevere commenti
in una settimana o poco più; se questo non dovesse accadere, assicuratevi di
aver inviato le patch correttamente.  Aspettate almeno una settimana prima di
rinviare le modifiche o sollecitare i revisori - probabilmente anche di più
durante la finestra d'integrazione.

10) Aggiungete PATCH nell'oggetto
---------------------------------

Dato l'alto volume di e-mail per Linus, e la lista linux-kernel, è prassi
prefiggere il vostro oggetto con [PATCH].  Questo permette a Linus e agli
altri sviluppatori del kernel di distinguere facilmente le patch dalle altre
discussioni.


11) Firmate il vostro lavoro - Il certificato d'origine dello sviluppatore
--------------------------------------------------------------------------

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

usando il vostro vero nome (spiacenti, non si accettano pseudonimi o
contributi anonimi).

Alcune persone aggiungono delle etichette alla fine.  Per ora queste verranno
ignorate, ma potete farlo per meglio identificare procedure aziendali interne o
per aggiungere dettagli circa la firma.

Se siete un manutentore di un sottosistema o di un ramo, qualche volta dovrete
modificare leggermente le patch che avete ricevuto al fine di poterle
integrare; questo perché il codice non è esattamente lo stesso nei vostri
sorgenti e in quelli dei vostri contributori.  Se rispettate rigidamente la
regola (c), dovreste chiedere al mittente di rifare la patch, ma questo è
controproducente e una totale perdita di tempo ed energia.  La regola (b)
vi permette di correggere il codice, ma poi diventa davvero maleducato cambiare
la patch di qualcuno e addossargli la responsabilità per i vostri bachi.
Per risolvere questo problema dovreste aggiungere una riga, fra l'ultimo
Signed-off-by e il vostro, che spiega la vostra modifica.  Nonostante non ci
sia nulla di obbligatorio, un modo efficace è quello di indicare il vostro
nome o indirizzo email fra parentesi quadre, seguito da una breve descrizione;
questo renderà abbastanza visibile chi è responsabile per le modifiche
dell'ultimo minuto.  Per esempio::

	Signed-off-by: Random J Developer <random@developer.example.org>
	[lucky@maintainer.example.org: struct foo moved from foo.c to foo.h]
	Signed-off-by: Lucky K Maintainer <lucky@maintainer.example.org>

Questa pratica è particolarmente utile se siete i manutentori di un ramo
stabile ma al contempo volete dare credito agli autori, tracciare e integrare
le modifiche, e proteggere i mittenti dalle lamentele.  Notate che in nessuna
circostanza è permessa la modifica dell'identità dell'autore (l'intestazione
From), dato che è quella che appare nei changelog.

Un appunto speciale per chi porta il codice su vecchie versioni.  Sembra che
sia comune l'utile pratica di inserire un'indicazione circa l'origine della
patch all'inizio del messaggio di commit (appena dopo la riga dell'oggetto)
al fine di migliorare la tracciabilità.  Per esempio, questo è quello che si
vede nel rilascio stabile 3.x-stable::

  Date:   Tue Oct 7 07:26:38 2014 -0400

    libata: Un-break ATA blacklist

    commit 1c40279960bcd7d52dbdf1d466b20d24b99176c8 upstream.

E qui quello che potrebbe vedersi su un kernel più vecchio dove la patch è
stata applicata::

    Date:   Tue May 13 22:12:27 2008 +0200

        wireless, airo: waitbusy() won't delay

        [backport of 2.6 commit b7acbdfbd1f277c1eb23f344f899cfa4cd0bf36a]

Qualunque sia il formato, questa informazione fornisce un importante aiuto
alle persone che vogliono seguire i vostri sorgenti, e quelle che cercano
dei bachi.


12) Quando utilizzare Acked-by:, Cc:, e Co-developed-by:
--------------------------------------------------------

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

13) Utilizzare Reported-by:, Tested-by:, Reviewed-by:, Suggested-by: e Fixes:
-----------------------------------------------------------------------------

L'etichetta Reported-by da credito alle persone che trovano e riportano i bachi
e si spera che questo possa ispirarli ad aiutarci nuovamente in futuro.
Rammentate che se il baco è stato riportato in privato, dovrete chiedere il
permesso prima di poter utilizzare l'etichetta Reported-by.

L'etichetta Tested-by: indica che la patch è stata verificata con successo
(su un qualche sistema) dalla persona citata.  Questa etichetta informa i
manutentori che qualche verifica è stata fatta, fornisce un mezzo per trovare
persone che possano verificare il codice in futuro, e garantisce che queste
stesse persone ricevano credito per il loro lavoro.

Reviewd-by:, invece, indica che la patch è stata revisionata ed è stata
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
che è stato fatto sulla patch.  L'etichetta Reviewd-by, quando fornita da
revisori conosciuti per la loro conoscenza sulla materia in oggetto e per la
loro serietà nella revisione, accrescerà le probabilità che la vostra patch
venga integrate nel kernel.

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


14) Il formato canonico delle patch
-----------------------------------

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
attendono dei commenti (*Request For Comments*).  Se ci sono quattro patch
nella serie, queste dovrebbero essere enumerate così: 1/4, 2/4, 3/4, 4/4.
Questo assicura che gli sviluppatori capiranno l'ordine in cui le patch
dovrebbero essere applicate, e per tracciare quelle che hanno revisionate o
che hanno applicato.

Un paio di esempi di oggetti::

    Subject: [PATCH 2/5] ext2: improve scalability of bitmap searching
    Subject: [PATCH v2 01/27] x86: fix eflags tracking

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
i messaggi di log per la patch che li tratta.  Se la patch corregge un errore
di compilazione, non sarà necessario includere proprio _tutto_ quello che
è uscito dal compilatore; aggiungete solo quello che è necessario per far si
che la vostra patch venga trovata.  Come nella ``summary phrase``, è importante
essere sia brevi che descrittivi.

La linea di demarcazione ``---`` serve essenzialmente a segnare dove finisce
il messaggio di changelog.

Aggiungere il ``diffstat`` dopo ``---`` è un buon uso di questo spazio, per
mostrare i file che sono cambiati, e il numero di file aggiunto o rimossi.
Un ``diffstat`` è particolarmente utile per le patch grandi.  Altri commenti
che sono importanti solo per i manutentori, quindi inadatti al changelog
permanente, dovrebbero essere messi qui.  Un buon esempio per questo tipo
di commenti potrebbe essere quello di descrivere le differenze fra le versioni
della patch.

Se includete un ``diffstat`` dopo ``---``, usate le opzioni ``-p 1 -w70``
cosicché i nomi dei file elencati non occupino troppo spazio (facilmente
rientreranno negli 80 caratteri, magari con qualche indentazione).
(``git`` genera di base dei diffstat adatti).

Maggiori dettagli sul formato delle patch nei riferimenti qui di seguito.

.. _it_explicit_in_reply_to:

15) Usare esplicitamente In-Reply-To nell'intestazione
------------------------------------------------------

Aggiungere manualmente In-Reply-To: nell'intestazione dell'e-mail
potrebbe essere d'aiuto per associare una patch ad una discussione
precedente, per esempio per collegare la correzione di un baco con l'e-mail
che lo riportava.  Tuttavia, per serie di patch multiple è generalmente
sconsigliato l'uso di In-Reply-To: per collegare precedenti versioni.
In questo modo versioni multiple di una patch non diventeranno un'ingestibile
giungla di riferimenti all'interno dei programmi di posta.  Se un collegamento
è utile, potete usare https://lkml.kernel.org/ per ottenere i collegamenti
ad una versione precedente di una serie di patch (per esempio, potete usarlo
per l'email introduttiva alla serie).

16) Inviare richieste ``git pull``
----------------------------------

Se avete una serie di patch, potrebbe essere più conveniente per un manutentore
tirarle dentro al repositorio del sottosistema attraverso l'operazione
``git pull``.  Comunque, tenete presente che prendere patch da uno sviluppatore
in questo modo richiede un livello di fiducia più alto rispetto a prenderle da
una lista di discussione.  Di conseguenza, molti manutentori sono riluttanti
ad accettare richieste di *pull*, specialmente dagli sviluppatori nuovi e
quindi sconosciuti.  Se siete in dubbio, potete fare una richiesta di *pull*
come messaggio introduttivo ad una normale pubblicazione di patch, così
il manutentore avrà la possibilità di scegliere come integrarle.

Una richiesta di *pull* dovrebbe avere nell'oggetto [GIT] o [PULL].
La richiesta stessa dovrebbe includere il nome del repositorio e quello del
ramo su una singola riga; dovrebbe essere più o meno così::

  Please pull from

      git://jdelvare.pck.nerim.net/jdelvare-2.6 i2c-for-linus

  to get these changes:

Una richiesta di *pull* dovrebbe includere anche un messaggio generico
che dica cos'è incluso, una lista delle patch usando ``git shortlog``, e una
panoramica sugli effetti della serie di patch con ``diffstat``.  Il modo più
semplice per ottenere tutte queste informazioni è, ovviamente, quello di
lasciar fare tutto a ``git`` con il comando ``git request-pull``.

Alcuni manutentori (incluso Linus) vogliono vedere le richieste di *pull*
da commit firmati con GPG; questo fornisce una maggiore garanzia sul fatto
che siate stati proprio voi a fare la richiesta.  In assenza di tale etichetta
firmata Linus, in particolare, non prenderà alcuna patch da siti pubblici come
GitHub.

Il primo passo verso la creazione di questa etichetta firmata è quello di
creare una chiave GNUPG ed averla fatta firmare da uno o più sviluppatori
principali del kernel.  Questo potrebbe essere difficile per i nuovi
sviluppatori, ma non ci sono altre vie.  Andare alle conferenze potrebbe
essere un buon modo per trovare sviluppatori che possano firmare la vostra
chiave.

Una volta che avete preparato la vostra serie di patch in ``git``, e volete che
qualcuno le prenda, create una etichetta firmata col comando ``git tag -s``.
Questo creerà una nuova etichetta che identifica l'ultimo commit della serie
contenente una firma creata con la vostra chiave privata.  Avrete anche
l'opportunità di aggiungere un messaggio di changelog all'etichetta; questo è
il posto ideale per descrivere gli effetti della richiesta di *pull*.

Se i sorgenti da cui il manutentore prenderà le patch non sono gli stessi del
repositorio su cui state lavorando, allora non dimenticatevi di caricare
l'etichetta firmata anche sui sorgenti pubblici.

Quando generate una richiesta di *pull*, usate l'etichetta firmata come
obiettivo.  Un comando come il seguente farà il suo dovere::

  git request-pull master git://my.public.tree/linux.git my-signed-tag


Riferimenti
-----------

Andrew Morton, "La patch perfetta" (tpp).
  <http://www.ozlabs.org/~akpm/stuff/tpp.txt>

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
  <https://lkml.org/lkml/2005/7/11/336>

Kernel Documentation/translations/it_IT/process/coding-style.rst:
  :ref:`Documentation/translations/it_IT/process/coding-style.rst <it_codingstyle>`

E-mail di Linus Torvalds sul formato canonico di una patch:
  <http://lkml.org/lkml/2005/4/7/183>

Andi Kleen, "Su come sottomettere patch del kernel"
  Alcune strategie su come sottomettere modifiche toste o controverse.

  http://halobates.de/on-submitting-patches.pdf
