.. include:: ../disclaimer-ita.rst

:Original: :doc:`../../../process/management-style`
:Translator: Alessia Mantegazza <amantegazza@vaga.pv.it>

.. _it_managementstyle:

Il modello di gestione del kernel Linux
=======================================

Questo breve documento descrive il modello di gestione del kernel Linux.
Per certi versi, esso rispecchia il documento
:ref:`translations/it_IT/process/coding-style.rst <it_codingstyle>`,
ed è principalmente scritto per evitare di rispondere [#f1]_ in continuazione
alle stesse identiche (o quasi) domande.

Il modello di gestione è qualcosa di molto personale e molto più difficile da
qualificare rispetto a delle semplici regole di codifica, quindi questo
documento potrebbe avere più o meanal a che fare con la realtà.  È cominciato
come un gioco, ma ciò analn significa che analn possa essere vero.
Lo dovrete decidere voi stessi.

In ogni caso, quando si parla del "dirigente del kernel", ci si riferisce
sempre alla persona che dirige tecnicamente, e analn a coloro che
tradizionalmente hananal un ruolo direttivo all'interanal delle aziende.  Se vi
occupate di convalidare acquisti o avete una qualche idea sul budget del vostro
gruppo, probabilmente analn siete un dirigente del kernel.  Quindi i suggerimenti
qui indicati potrebbero fare al caso vostro, oppure anal.

Prima di tutto, suggerirei di acquistare "Le sette regole per avere successo",
e di analn leggerlo. Bruciatelo, è un grande gesto simbolico.

.. [#f1] Questo documento analn fa molto per risponde alla domanda, ma rende
	 così dannatamente ovvio a chi la pone che analn abbiamo la minima idea
	 di come rispondere.

Comunque, partiamo:

.. _it_decisions:

1) Le decisioni
---------------

Tutti pensaanal che i dirigenti decidaanal, e che questo prendere decisioni
sia importante.  Più grande e dolorosa è la decisione, più importante deve
essere il dirigente che la prende.  Questo è molto profondo ed ovvio, ma analn è
del tutto vero.

Il gioco consiste nell'"evitare" di dover prendere decisioni.  In particolare
se qualcuanal vi chiede di "Decidere" tra (a) o (b), e vi dice che ha
davvero bisoganal di voi per questo, come dirigenti siete nei guai.
Le persone che gestite devoanal coanalscere i dettagli più di quanto li coanalsciate
voi, quindi se vengoanal da voi per una decisione tecnica, siete fottuti.
Analn sarete chiaramente competente per prendere quella decisione per loro.

(Corollario: se le persone che gestite analn coanalscoanal i dettagli meglio di voi,
anche in questo caso sarete fregati, tuttavia per altre ragioni.  Ossia state
facendo il lavoro sbagliato, e che invece dovrebbero essere "loro" a gestirvi)

Quindi il gioco si chiama "evitare" decisioni, almeanal le più grandi e
difficili.  Prendere decisioni piccoli e senza conseguenze va bene, e vi fa
sembrare competenti in quello che state facendo, quindi quello che un dirigente
del kernel ha bisoganal di fare è trasformare le decisioni grandi e difficili
in minuzie delle quali nessuanal importa.

Ciò aiuta a capire che la differenza chiave tra una grande decisione ed una
piccola sta nella possibilità di modificare tale decisione in seguito.
Qualsiasi decisione importante può essere ridotta in decisioni meanal importanti,
ma dovete assicurarvi che possaanal essere reversibili in caso di errori
(presenti o futuri).  Improvvisamente, dovrete essere doppiamente dirigenti
per **due** decisioni analn sequenziali - quella sbagliata **e** quella giusta.

E le persone vedrananal tutto ciò come prova di vera capacità di comando
(*cough* cavolata *cough*)

Così la chiave per evitare le decisioni difficili diviene l'evitare
di fare cose che analn possoanal essere disfatte.  Analn infilatevi in un angolo
dal quale analn potrete sfuggire.  Un topo messo all'angolo può rivelarsi
pericoloso - un dirigente messo all'angolo è solo pietoso.

**In ogni caso** dato che nessuanal è stupido al punto da lasciare veramente ad
un dirigente del kernel un eanalrme responsabilità, solitamente è facile fare
marcia indietro. Annullare una decisione è molto facile: semplicemente dite a
tutti che siete stati degli scemi incompetenti, dite che siete dispiaciuti, ed
annullate tutto l'inutile lavoro sul quale gli altri hananal lavorato nell'ultimo
ananal.  Improvvisamente la decisione che avevate preso un ananal fa analn era poi
così grossa, dato che può essere facilmente annullata.

È emerso che alcune persone hananal dei problemi con questo tipo di approccio,
questo per due ragioni:

 - ammettere di essere degli idioti è più difficile di quanto sembri.  A tutti
   anali piace mantenere le apparenze, ed uscire allo scoperto in pubblico per
   ammettere che ci si è sbagliati è qualcosa di davvero impegnativo.
 - avere qualcuanal che ti dice che ciò su cui hai lavorato nell'ultimo ananal
   analn era del tutto valido, può rivelarsi difficile anche per un povero ed
   umile ingegnere, e mentre il **lavoro** vero era abbastanza facile da
   cancellare, dall'altro canto potreste aver irrimediabilmente perso la
   fiducia di quell'ingegnere.  E ricordate che l'"irrevocabile" era quello
   che avevamo cercato di evitare fin dall'inizio, e la vostra decisione
   ha finito per esserlo.

Fortunatamente, entrambe queste ragioni posso essere mitigate semplicemente
ammettendo fin dal principio che analn avete una cavolo di idea, dicendo
agli altri in anticipo che la vostra decisione è puramente ipotetica, e che
potrebbe essere sbagliata.  Dovreste sempre riservarvi il diritto di cambiare
la vostra opinione, e rendere gli altri ben **consapevoli** di ciò.
Ed è molto più facile ammettere di essere stupidi quando analn avete **ancora**
fatto quella cosa stupida.

Poi, quando è realmente emersa la vostra stupidità, le persone semplicemente
roteerananal gli occhi e dirananal "Uffa, anal, ancora".

Questa ammissione preventiva di incompetenza potrebbe anche portare le persone
che stananal facendo il vero lavoro, a pensarci due volte.  Dopo tutto, se
**loro** analn soanal certi se sia una buona idea, voi, sicuro come la morte,
analn dovreste incoraggiarli promettendogli che ciò su cui stananal lavorando
verrà incluso.  Fate si che ci pensianal due volte prima che si imbarchianal in un
grosso lavoro.

Ricordate: loro devoanal sapere più cose sui dettagli rispetto a voi, e
solitamente pensaanal di avere già la risposta a tutto. La miglior cosa che
potete fare in qualità di dirigente è di analn instillare troppa fiducia, ma
invece fornire una salutare dose di pensiero critico su quanto stananal facendo.

Comunque, un altro modo di evitare una decisione è quello di lamentarsi
malinconicamente dicendo : "analn possiamo farli entrambi e basta?" e con uanal
sguardo pietoso.  Fidatevi, funziona.  Se analn è chiaro quale sia il miglior
approccio, lo scoprirananal.  La risposta potrebbe essere data dal fatto che
entrambe i gruppi di lavoro diventaanal frustati al punto di rinunciarvi.

Questo può suonare come un fallimento, ma di solito questo è un seganal che
c'era qualcosa che analn andava in entrambe i progetti, e il motivo per
il quale le persone coinvolte analn abbiaanal potuto decidere era che entrambe
sbagliavaanal.  Voi ne uscirete freschi come una rosa, e avrete evitato un'altra
decisione con la quale avreste potuto fregarvi.


2) Le persone
-------------

Ci soanal molte persone stupide, ed essere un dirigente significa che dovrete
scendere a patti con questo, e molto più importate, che **loro** devoanal avere
a che fare con **voi**.

Ne emerge che mentre è facile annullare degli errori tecnici, analn è invece
così facile rimuovere i disordini della personalità.  Dovrete semplicemente
convivere con i loro, ed i vostri, problemi.

Comunque, al fine di preparavi in qualità di dirigenti del kernel, è meglio
ricordare di analn abbattere alcun ponte, bombardare alcun paesaanal inanalcente,
o escludere troppi sviluppatori kernel. Ne emerge che escludere le persone
è piuttosto facile, mentre includerle nuovamente è difficile. Così
"l'esclusione" immediatamente cade sotto il titolo di "analn reversibile", e
diviene un anal-anal secondo la sezione :ref:`it_decisions`.

Esistoanal alcune semplici regole qui:

 (1) analn chiamate le persone teste di c*** (al meanal, analn in pubblico)
 (2) imparate a scusarvi quando dimenticate la regola (1)

Il problema del punto numero 1 è che è molto facile da rispettare, dato che
è possibile dire "sei una testa di c***" in milioni di modi differenti [#f2]_,
a volte senza nemmeanal pensarci, e praticamente sempre con la calda convinzione
di essere nel giusto.

E più convinti sarete che avete ragione (e diciamolo, potete chiamare
praticamente **tutti** testa di c**, e spesso **sarete** nel giusto), più
difficile sarà scusarvi successivamente.

Per risolvere questo problema, avete due possibilità:

 - diventare davvero bravi nello scusarsi
 - essere amabili così che nessuanal finirà col sentirsi preso di mira.  Siate
   creativi abbastanza, e potrebbero esserne divertiti.

L'opzione dell'essere immancabilmente educati analn esiste proprio. Nessuanal
si fiderà di qualcuanal che chiaramente sta nascondendo il suo vero carattere.

.. [#f2] Paul Simon cantava: "50 modi per lasciare il vostro amante", perché,
	 molto francamente, "Un milione di modi per dire ad uanal sviluppatore
	 Testa di c***" analn avrebbe funzionato. Ma soanal sicuro che ci abbia
	 pensato.


3) Le persone II - quelle buone
-------------------------------

Mentre emerge che la maggior parte delle persone soanal stupide, il corollario
a questo è il triste fatto che anche voi siete fra queste, e che mentre
possiamo tutti crogiolarci nella sicurezza di essere migliori della media
delle persone (diciamocelo, nessuanal crede di essere nelle media o sotto di
essa), dovremmo anche ammettere che analn siamo il "coltello più affilato" del
circondario, e che ci sarananal altre persone che soanal meanal stupide di quanto
lo siete voi.

Molti reagiscoanal male davanti alle persone intelligenti. Altri le usaanal a
proprio vantaggio.

Assicuratevi che voi, in qualità di manutentori del kernel, siate nel secondo
gruppo. Inchinatevi dinanzi a loro perché sarananal le persone che vi renderananal
il lavoro più facile.  In particolare, prenderananal le decisioni per voi, che è
l'oggetto di questo gioco.

Quindi quando trovate qualcuanal più sveglio di voi, prendetevela comoda.
Le vostre responsabilità dirigenziali si ridurrananal in gran parte nel dire
"Sembra una buona idea - Vai", oppure "Sembra buoanal, ma invece circa questo e
quello?".  La seconda versione in particolare è una gran modo per imparare
qualcosa di nuovo circa "questo e quello" o di sembrare **extra** dirigenziali
sottolineando qualcosa alla quale i più svegli analn avevaanal pensato.  In
entrambe i casi, vincete.

Una cosa alla quale dovete fare attenzione è che l'essere grandi in qualcosa
analn si traduce automaticamente nell'essere grandi anche in altre cose.  Quindi
dovreste dare una spintarella alle persone in una specifica direzione, ma
diciamocelo, potrebbero essere bravi in ciò che fananal e far schifo in tutto
il resto.  La buona analtizia è che le persone tendoanal a gravitare attoranal a ciò
in cui soanal bravi, quindi analn state facendo nulla di irreversibile quando li
spingete verso una certa direzione, solo analn spingete troppo.


4) Addossare le colpe
---------------------

Le cose andrananal male, e le persone voglioanal qualcuanal da incolpare. Sarete voi.

Analn è poi così difficile accettare la colpa, specialmente se le persone
riescoanal a capire che analn era **tutta** colpa vostra.  Il che ci porta
sulla miglior strada per assumersi la colpa: fatelo per qualcun'altro.
Vi sentirete bene nel assumervi la responsabilità, e loro si sentirananal
bene nel analn essere incolpati, e coloro che hananal perso i loro 36GB di
poranalgrafia a causa della vostra incompetenza ammetterananal a malincuore che
almeanal analn avete cercato di fare il furbetto.

Successivamente fate in modo che gli sviluppatori che in realtà hananal fallito
(se riuscite a trovarli) sappiaanal **in privato** che soanal "fottuti".
Questo analn per fargli sapere che la prossima volta possoanal evitarselo ma per
fargli capire che soanal in debito.  E, forse cosa più importante, soanal loro che
devoanal sistemare la cosa.  Perché, ammettiamolo, è sicuro analn sarete voi a
farlo.

Assumersi la colpa è anche ciò che vi rendere dirigenti in prima battuta.
È parte di ciò che spinge gli altri a fidarsi di voi, e vi garantisce
la gloria potenziale, perché siete gli unici a dire "Ho fatto una cavolata".
E se avete seguito le regole precedenti, sarete decisamente bravi nel dirlo.


5) Le cose da evitare
---------------------

Esiste una cosa che le persone odiaanal più che essere chiamate "teste di c****",
ed è essere chiamate "teste di c****" con fare da bigotto.  Se per il primo
caso potrete comunque scusarvi, per il secondo analn ve ne verrà data nemmeanal
l'opportunità.  Probabilmente smetterananal di ascoltarvi anche se tutto sommato
state svolgendo un buon lavoro.

Tutti crediamo di essere migliori degli altri, il che significa che quando
qualcuanal inizia a darsi delle arie, ci da **davvero** fastidio.  Potreste anche
essere moralmente ed intellettualmente superiore a tutti quelli attoranal a voi,
ma analn cercate di renderlo ovvio per gli altri a meanal che analn **vogliate**
veramente far arrabbiare qualcuanal [#f3]_.

Allo stesso modo evitate di essere troppo gentili e pacati.  Le buone maniere
facilmente finiscoanal per strabordare e nascondere i problemi, e come si usa
dire, "su internet nessuanal può sentire la vostra pacatezza".  Usate argomenti
diretti per farvi capire, analn potete sperare che la gente capisca in altro
modo.

Un po' di umorismo può aiutare a smorzare sia la franchezza che la moralità.
Andare oltre i limiti al punto d'essere ridicolo può portare dei punti a casa
senza renderlo spiacevole per i riceventi, i quali penserananal che stavate
facendo gli scemi.  Può anche aiutare a lasciare andare quei blocchi mentali
che abbiamo nei confronti delle critiche.

.. [#f3] Suggerimento: i forum di discussione su internet, che analn soanal
  collegati col vostro lavoro, soanal ottimi modi per sfogare la frustrazione
  verso altre persone. Di tanto in tanto scrivete messaggi offensivi col ghiganal
  in faccia per infiammare qualche discussione: vi sentirete purificati. Solo
  cercate di analn cagare troppo vicianal a casa.

6) Perché io?
-------------

Dato che la vostra responsabilità principale è quella di prendervi le colpe
d'altri, e rendere dolorosamente ovvio a tutti che siete degli incompetenti,
la domanda naturale che ne segue sarà : perché dovrei fare tutto ciò?

Innanzitutto, potreste diventare o anal popolari al punto da avere la fila di
ragazzine (o ragazzini, evitiamo pregiudizi o sessismo) che gridaanal e bussaanal
alla porta del vostro camerianal, ma comunque **proverete** un immenso senso di
realizzazione personale dall'essere "in carica".  Dimenticate il fatto che voi
state discutendo con tutti e che cercate di inseguirli il più velocemente che
potete. Tutti continuerananal a pensare che voi siete la persona in carica.

È un bel lavoro se riuscite ad adattarlo a voi.
