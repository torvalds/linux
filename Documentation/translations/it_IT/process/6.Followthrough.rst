.. include:: ../disclaimer-ita.rst

:Original: :ref:`Documentation/process/6.Followthrough.rst <development_followthrough>`
:Translator: Alessia Mantegazza <amantegazza@vaga.pv.it>

.. _it_development_followthrough:

=============
Completamento
=============

A questo punto, avete seguito le linee guida fino a questo punto e, con
l'aggiunta delle vostre capacità ingegneristiche, avete pubblicato una serie
perfetta di patch.  Uno dei più grandi errori che possono essere commessi
persino da sviluppatori kernel esperti è quello di concludere che il
lavoro sia ormai finito.  In verità, la pubblicazione delle patch
simboleggia una transizione alla fase successiva del processo, con,
probabilmente, ancora un po' di lavoro da fare.

È raro che una modifica sia così bella alla sua prima pubblicazione che non
ci sia alcuno spazio di miglioramento.  Il programma di sviluppo del kernel
riconosce questo fatto e quindi, è fortemente orientato al miglioramento
del codice pubblicato.  Voi, in qualità di autori del codice, dovrete
lavorare con la comunità del kernel per assicurare che il vostro codice
mantenga gli standard qualitativi richiesti.  Un fallimento in questo
processo è quasi come impedire l'inclusione delle vostre patch nel
ramo principale.

Lavorare con i revisori
=======================

Una patch che abbia una certa rilevanza avrà ricevuto numerosi commenti
da parte di altri sviluppatori dato che avranno revisionato il codice.
Lavorare con i revisori può rivelarsi, per molti sviluppatori, la parte
più intimidatoria del processo di sviluppo del kernel.  La vita può esservi
resa molto più facile se tenete presente alcuni dettagli:

 - Se avete descritto la vostra modifica correttamente, i revisori ne
   comprenderanno il valore e il perché vi siete presi il disturbo di
   scriverla.  Ma tale valore non li tratterrà dal porvi una domanda
   fondamentale: come verrà mantenuto questo codice nel kernel nei prossimi
   cinque o dieci anni?  Molti dei cambiamenti che potrebbero esservi
   richiesti - da piccoli problemi di stile a sostanziali ristesure -
   vengono dalla consapevolezza che Linux resterà in circolazione e in
   continuo sviluppo ancora per diverse decadi.

 - La revisione del codice è un duro lavoro, ed è un mestiere poco
   riconosciuto; le persone ricordano chi ha scritto il codice, ma meno
   fama è attribuita a chi lo ha revisionato.  Quindi i revisori potrebbero
   divenire burberi, specialmente quando vendono i medesimi errori venire
   fatti ancora e ancora.  Se ricevete una revisione che vi sembra abbia
   un tono arrabbiato, insultante o addirittura offensivo, resistente alla
   tentazione di rispondere a tono.  La revisione riguarda il codice e non
   la persona, e i revisori non vi stanno attaccando personalmente.

 - Similarmente, i revisori del codice non stanno cercando di promuovere
   i loro interessi a vostre spese.  Gli sviluppatori del kernel spesso si
   aspettano di lavorare sul kernel per anni, ma sanno che il loro datore
   di lavoro può cambiare.  Davvero, senza praticamente eccezioni, loro
   stanno lavorando per la creazione del miglior kernel possibile; non
   stanno cercando di creare un disagio ad aziende concorrenti.

Quello che si sta cercando di dire è che, quando i revisori vi inviano degli
appunti dovete fare attenzione alle osservazioni tecniche che vi stanno
facendo.  Non lasciate che il loro modo di esprimersi o il vostro orgoglio
impediscano che ciò accada.  Quando avete dei suggerimenti sulla revisione,
prendetevi il tempo per comprendere cosa il revisore stia cercando di
comunicarvi.  Se possibile, sistemate le cose che il revisore vi chiede di
modificare.  E rispondete al revisore ringraziandolo e spiegando come
intendete fare.

Notate che non dovete per forza essere d'accordo con ogni singola modifica
suggerita dai revisori.  Se credete che il revisore non abbia compreso
il vostro codice, spiegateglielo.  Se avete un'obiezione tecnica da fargli
su di una modifica suggerita, spiegatela inserendo anche la vostra soluzione
al problema.  Se la vostra spiegazione ha senso, il revisore la accetterà.
Tuttavia, la vostra motivazione potrebbe non essere del tutto persuasiva,
specialmente se altri iniziano ad essere d'accordo con il revisore.
Prendetevi quindi un po' di tempo per pensare ancora alla cosa. Può risultare
facile essere accecati dalla propria soluzione al punto che non realizzate che
c'è qualcosa di fondamentalmente sbagliato o, magari, non state nemmeno
risolvendo il problema giusto.

Andrew Morton suggerisce che ogni suggerimento di revisione che non è
presente nella modifica del codice dovrebbe essere inserito in un commento
aggiuntivo; ciò può essere d'aiuto ai futuri revisori nell'evitare domande
che sorgono al primo sguardo.

Un errore fatale è quello di ignorare i commenti di revisione nella speranza
che se ne andranno.  Non andranno via.  Se pubblicherete nuovamente il
codice senza aver risposto ai commenti ricevuti, probabilmente le vostre
modifiche non andranno da nessuna parte.

Parlando di ripubblicazione del codice: per favore tenete a mente che i
revisori non ricorderanno tutti i dettagli del codice che avete pubblicato
l'ultima volta. Quindi è sempre una buona idea quella di ricordare ai
revisori le questioni sollevate precedetemene e come le avete risolte.
I revisori non dovrebbero star lì a cercare all'interno degli archivi per
famigliarizzare con ciò che è stato detto l'ultima volta; se li aiutate
in questo senso, saranno di umore migliore quando riguarderanno il vostro
codice.

Se invece avete cercato di far tutto correttamente ma le cose continuano
a non andar bene?  Molti disaccordi di natura tecnica possono essere risolti
attraverso la discussione, ma ci sono volte dove qualcuno deve prendere
una decisione.  Se credete veramente che tale decisione andrà contro di voi
ingiustamente, potete sempre tentare di rivolgervi a qualcuno più
in alto di voi.  Per cose di questo genere la persona con più potere è
Andrew Morton.  Andrew è una figura molto rispettata all'interno della
comunità di sviluppo del kernel; lui può spesso sbrogliare situazioni che
sembrano irrimediabilmente bloccate.  Rivolgersi ad Andrew non deve essere
fatto alla leggera, e non deve essere fatto prima di aver esplorato tutte
le altre alternative.  E tenete a mente, ovviamente, che nemmeno lui
potrebbe non essere d'accordo con voi.

Cosa accade poi
===============

Se la modifica è ritenuta un elemento valido da essere aggiunta al kernel,
e una volta che la maggior parte degli appunti dei revisori sono stati
sistemati, il passo successivo solitamente è quello di entrare in un
sottosistema gestito da un manutentore.  Come ciò avviene dipende dal
sottosistema medesimo; ogni manutentore ha il proprio modo di fare le cose.
In particolare, ci potrebbero essere diversi sorgenti - uno, magari, dedicato
alle modifiche pianificate per la finestra di fusione successiva, e un altro
per il lavoro di lungo periodo.

Per le modifiche proposte in aree per le quali non esiste un sottosistema
preciso (modifiche di gestione della memoria, per esempio), i sorgenti di
ripiego finiscono per essere -mm.  Ed anche le modifiche che riguardano
più sottosistemi possono finire in quest'ultimo.

L'inclusione nei sorgenti di un sottosistema può comportare per una patch,
un alto livello di visibilità.  Ora altri sviluppatori che stanno lavorando
in quei medesimi sorgenti avranno le vostre modifiche.  I sottosistemi
solitamente riforniscono anche Linux-next, rendendo i propri contenuti
visibili all'intera comunità di sviluppo.  A questo punto, ci sono buone
possibilità per voi di ricevere ulteriori commenti da un nuovo gruppo di
revisori; anche a questi commenti dovrete rispondere come avete già fatto per
gli altri.

Ciò che potrebbe accadere a questo punto, in base alla natura della vostra
modifica, riguarda eventuali conflitti con il lavoro svolto da altri.
Nella peggiore delle situazioni, i conflitti più pesanti tra modifiche possono
concludersi con la messa a lato di alcuni dei lavori svolti cosicché le
modifiche restanti possano funzionare ed essere integrate.  Altre volte, la
risoluzione dei conflitti richiederà del lavoro con altri sviluppatori e,
possibilmente, lo spostamento di alcune patch da dei sorgenti a degli altri
in modo da assicurare che tutto sia applicato in modo pulito.  Questo lavoro
può rivelarsi una spina nel fianco, ma consideratevi fortunati: prima
dell'avvento dei sorgenti linux-next, questi conflitti spesso emergevano solo
durante l'apertura della finestra di integrazione e dovevano essere smaltiti
in fretta.  Ora essi possono essere risolti comodamente, prima dell'apertura
della finestra.

Un giorno, se tutto va bene, vi collegherete e vedrete che la vostra patch
è stata inserita nel ramo principale de kernel. Congratulazioni!  Terminati
i festeggiamenti (nel frattempo avrete inserito il vostro nome nel file
MAINTAINERS) vale la pena ricordare una piccola cosa, ma importante: il
lavoro non è ancora finito.  L'inserimento nel ramo principale porta con se
nuove sfide.

Cominciamo con il dire che ora la visibilità della vostra modifica è
ulteriormente cresciuta.  Ci potrebbe portare ad una nuova fase di
commenti dagli sviluppatori che non erano ancora a conoscenza della vostra
patch.  Ignorarli potrebbe essere allettante dato che non ci sono più
dubbi sull'integrazione della modifica.  Resistete a tale tentazione, dovete
mantenervi disponibili agli sviluppatori che hanno domande o suggerimenti
per voi.

Ancora più importante: l'inclusione nel ramo principale mette il vostro
codice nelle mani di un gruppo di *tester* molto più esteso.  Anche se avete
contribuito ad un driver per un hardware che non è ancora disponibile, sarete
sorpresi da quante persone inseriranno il vostro codice nei loro kernel.
E, ovviamente, dove ci sono *tester*, ci saranno anche dei rapporti su
eventuali bachi.

La peggior specie di rapporti sono quelli che indicano delle regressioni.
Se la vostra modifica causa una regressione, avrete un gran numero di
occhi puntati su di voi; la regressione deve essere sistemata il prima
possibile.  Se non vorrete o non sarete capaci di sistemarla (e nessuno
lo farà per voi), la vostra modifica sarà quasi certamente rimossa durante
la fase di stabilizzazione.  Oltre alla perdita di tutto il lavoro svolto
per far si che la vostra modifica fosse inserita nel ramo principale,
l'avere una modifica rimossa a causa del fallimento nel sistemare una
regressione, potrebbe rendere più difficile per voi far accettare
il vostro lavoro in futuro.

Dopo che ogni regressione è stata affrontata, ci potrebbero essere altri
bachi ordinari da "sconfiggere".  Il periodo di stabilizzazione è la
vostra migliore opportunità per sistemare questi bachi e assicurarvi che
il debutto del vostro codice nel ramo principale del kernel sia il più solido
possibile.  Quindi, per favore, rispondete ai rapporti sui bachi e ponete
rimedio, se possibile, a tutti i problemi.  È a questo che serve il periodo
di stabilizzazione; potete iniziare creando nuove fantastiche modifiche
una volta che ogni problema con le vecchie sia stato risolto.

Non dimenticate che esistono altre pietre miliari che possono generare
rapporti sui bachi: il successivo rilascio stabile, quando una distribuzione
importante usa una versione del kernel nel quale è presente la vostra
modifica, eccetera.  Il continuare a rispondere a questi rapporti è fonte di
orgoglio per il vostro lavoro.  Se questa non è una sufficiente motivazione,
allora, è anche consigliabile considera che la comunità di sviluppo ricorda
gli sviluppatori che hanno perso interesse per il loro codice una volta
integrato.  La prossima volta che pubblicherete una patch, la comunità
la valuterà anche sulla base del fatto che non sarete disponibili a
prendervene cura anche nel futuro.


Altre cose che posso accadere
=============================

Un giorno, potreste aprire la vostra email e vedere che qualcuno vi ha
inviato una patch per il vostro codice.  Questo, dopo tutto, è uno dei
vantaggi di avere il vostro codice "là fuori".  Se siete d'accordo con
la modifica, potrete anche inoltrarla ad un manutentore di sottosistema
(assicuratevi di includere la riga "From:" cosicché l'attribuzione sia
corretta, e aggiungete una vostra firma "Signed-off-by"), oppure inviate
un "Acked-by:" e lasciate che l'autore originale la invii.

Se non siete d'accordo con la patch, inviate una risposta educata
spiegando il perché.  Se possibile, dite all'autore quali cambiamenti
servirebbero per rendere la patch accettabile da voi.  C'è una certa
riluttanza nell'inserire modifiche con un conflitto fra autore
e manutentore del codice, ma solo fino ad un certo punto.  Se siete visti
come qualcuno che blocca un buon lavoro senza motivo, quelle patch vi
passeranno oltre e andranno nel ramo principale in ogni caso. Nel kernel
Linux, nessuno ha potere di veto assoluto su alcun codice.  Eccezione
fatta per Linus, forse.

In rarissime occasioni, potreste vedere qualcosa di completamente diverso:
un altro sviluppatore che pubblica una soluzione differente al vostro
problema.  A questo punto, c'è una buona probabilità che una delle due
modifiche non verrà integrata, e il "c'ero prima io" non è considerato
un argomento tecnico rilevante.  Se la modifica di qualcun'altro rimpiazza
la vostra ed entra nel ramo principale, esiste un unico modo di reagire:
siate contenti che il vostro problema sia stato risolto e andate avanti con
il vostro lavoro.  L'avere un vostro lavoro spintonato da parte in questo
modo può essere avvilente e scoraggiante, ma la comunità ricorderà come
avrete reagito anche dopo che avrà dimenticato quale fu la modifica accettata.
