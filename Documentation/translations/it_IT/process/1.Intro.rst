.. include:: ../disclaimer-ita.rst

:Original: :ref:`Documentation/process/1.Intro.rst <development_process_intro>`
:Translator: Alessia Mantegazza <amantegazza@vaga.pv.it>

.. _it_development_intro:

Introduzione
============

Riepilogo generale
------------------

Il resto di questa sezione riguarda il processo di sviluppo del kernel e
quella sorta di frustrazione che gli sviluppatori e i loro datori di lavoro
potrebbero dover affrontare.  Ci sono molte ragioni per le quali del codice
per il kernel debba essere incorporato nel kernel ufficiale, fra le quali:
disponibilità immediata agli utilizzatori, supporto della comunità in
differenti modalità, e la capacità di influenzare la direzione dello sviluppo
del kernel.
Il codice che contribuisce al kernel Linux deve essere reso disponibile sotto
una licenza GPL-compatibile.

La sezione :ref:`it_development_process` introduce il processo di sviluppo,
il ciclo di rilascio del kernel, ed i meccanismi della finestra
d'incorporazione.  Il capitolo copre le varie fasi di una modifica: sviluppo,
revisione e ciclo d'incorporazione. Ci sono alcuni dibattiti su strumenti e
liste di discussione. Gli sviluppatori che sono in attesa di poter sviluppare
qualcosa per il kernel sono invitati ad individuare e sistemare bachi come
esercizio iniziale.

La sezione :ref:`it_development_early_stage` copre i primi stadi della
pianificazione di un progetto di sviluppo, con particolare enfasi sul
coinvolgimento della comunità, il prima possibile.

La sezione :ref:`it_development_coding` riguarda il processo di scrittura
del codice. Qui, sono esposte le diverse insidie che sono state già affrontate
da altri sviluppatori.  Il capitolo copre anche alcuni dei requisiti per le
modifiche, ed esiste un'introduzione ad alcuni strumenti che possono aiutarvi
nell'assicurarvi che le modifiche per il kernel siano corrette.

La sezione :ref:`it_development_posting` parla del processo di pubblicazione
delle modifiche per la revisione. Per essere prese in considerazione dalla
comunità di sviluppo, le modifiche devono essere propriamente formattate ed
esposte, e devono essere inviate nel posto giusto. Seguire i consigli presenti
in questa sezione dovrebbe essere d'aiuto nell'assicurare la migliore
accoglienza possibile del vostro lavoro.

La sezione :ref:`it_development_followthrough` copre ciò che accade dopo
la pubblicazione delle modifiche; a questo punto il lavoro è lontano
dall'essere concluso.  Lavorare con i revisori è una parte cruciale del
processo di sviluppo; questa sezione offre una serie di consigli su come
evitare problemi in questa importante fase.  Gli sviluppatori sono diffidenti
nell'affermare che il lavoro è concluso quando una modifica è incorporata nei
sorgenti principali.

La sezione :ref:`it_development_advancedtopics` introduce un paio di argomenti
"avanzati": gestire le modifiche con git e controllare le modifiche pubblicate
da altri.

La sezione :ref:`it_development_conclusion` chiude il documento con dei
riferimenti ad altre fonti che forniscono ulteriori informazioni sullo sviluppo
del kernel.

Di cosa parla questo documento
------------------------------

Il kernel Linux, ha oltre 8 milioni di linee di codice e ben oltre 1000
contributori ad ogni rilascio; è uno dei più vasti e più attivi software
liberi progettati mai esistiti.  Sin dal sul modesto inizio nel 1991,
questo kernel si è evoluto nel miglior componente per sistemi operativi
che fanno funzionare piccoli riproduttori musicali, PC, grandi super computer
e tutte le altre tipologie di sistemi fra questi estremi.  È una soluzione
robusta, efficiente ed adattabile a praticamente qualsiasi situazione.

Con la crescita di Linux è arrivato anche un aumento di sviluppatori
(ed aziende) desiderosi di partecipare a questo sviluppo. I produttori di
hardware vogliono assicurarsi che il loro prodotti siano supportati da Linux,
rendendo questi prodotti attrattivi agli utenti Linux.  I produttori di
sistemi integrati, che usano Linux come componente di un prodotto integrato,
vogliono che Linux sia capace ed adeguato agli obiettivi ed il più possibile
alla mano. Fornitori ed altri produttori di software che basano i propri
prodotti su Linux hanno un chiaro interesse verso capacità, prestazioni ed
affidabilità del kernel Linux.  E gli utenti finali, anche, spesso vorrebbero
cambiare Linux per renderlo più aderente alle proprie necessità.

Una delle caratteristiche più coinvolgenti di Linux è quella dell'accessibilità
per gli sviluppatori; chiunque con le capacità richieste può migliorare
Linux ed influenzarne la direzione di sviluppo.  Prodotti non open-source non
possono offrire questo tipo di apertura, che è una caratteristica del software
libero.  Ma, anzi, il kernel è persino più aperto rispetto a molti altri
progetti di software libero.  Un classico ciclo di sviluppo trimestrale può
coinvolgere 1000 sviluppatori che lavorano per più di 100 differenti aziende
(o per nessuna azienda).

Lavorare con la comunità di sviluppo del kernel non è particolarmente
difficile.  Ma, ciononostante, diversi potenziali contributori hanno trovato
delle difficoltà quando hanno cercato di lavorare sul kernel.  La comunità del
kernel utilizza un proprio modo di operare che gli permette di funzionare
agevolmente (e genera un prodotto di alta qualità) in un ambiente dove migliaia
di stringhe di codice sono modificate ogni giorni. Quindi non deve sorprendere
che il processo di sviluppo del kernel differisca notevolmente dai metodi di
sviluppo privati.

Il processo di sviluppo del Kernel può, dall'altro lato, risultare
intimidatorio e strano ai nuovi sviluppatori, ma ha dietro di se buone ragioni
e solide esperienze.  Uno sviluppatore che non comprende i modi della comunità
del kernel (o, peggio, che cerchi di aggirarli o violarli) avrà un'esperienza
deludente nel proprio bagaglio.  La comunità di sviluppo, sebbene sia utile
a coloro che cercano di imparare, ha poco tempo da dedicare a coloro che non
ascoltano o coloro che non sono interessati al processo di sviluppo.

Si spera che coloro che leggono questo documento saranno in grado di evitare
queste esperienze spiacevoli.  C'è  molto materiale qui, ma lo sforzo della
lettura sarà ripagato in breve tempo.  La comunità di sviluppo ha sempre
bisogno di sviluppatori che vogliano aiutare a rendere il kernel migliore;
il testo seguente potrebbe esservi d'aiuto - o essere d'aiuto ai vostri
collaboratori- per entrare a far parte della nostra comunità.

Crediti
-------

Questo documento è stato scritto da Jonathan Corbet, corbet@lwn.net.
È stato migliorato da Johannes Berg, James Berry, Alex Chiang, Roland
Dreier, Randy Dunlap, Jake Edge, Jiri Kosina, Matt Mackall, Arthur Marsh,
Amanda McPherson, Andrew Morton, Andrew Price, Tsugikazu Shibata e Jochen Voß.

Questo lavoro è stato supportato dalla Linux Foundation; un ringraziamento
speciale ad Amanda McPherson, che ha visto il valore di questo lavoro e lo ha
reso possibile.

L'importanza d'avere il codice nei sorgenti principali
------------------------------------------------------

Alcune aziende e sviluppatori ogni tanto si domandano perché dovrebbero
preoccuparsi di apprendere come lavorare con la comunità del kernel e di
inserire il loro codice nel ramo di sviluppo principale (per ramo principale
s'intende quello mantenuto da Linus Torvalds e usato come base dai
distributori Linux). Nel breve termine, contribuire al codice può sembrare
un costo inutile; può sembra più facile tenere separato il proprio codice e
supportare direttamente i suoi utilizzatori. La verità è che il tenere il
codice separato ("fuori dai sorgenti", *"out-of-tree"*) è un falso risparmio.

Per dimostrare i costi di un codice "fuori dai sorgenti", eccovi
alcuni aspetti rilevanti del processo di sviluppo kernel; la maggior parte
di essi saranno approfonditi dettagliatamente più avanti in questo documento.
Considerate:

- Il codice che è stato inserito nel ramo principale del kernel è disponibile
  a tutti gli utilizzatori Linux. Sarà automaticamente presente in tutte le
  distribuzioni che lo consentono. Non c'è bisogno di: driver per dischi,
  scaricare file, o della scocciatura del dover supportare diverse versioni di
  diverse distribuzioni; funziona già tutto, per gli sviluppatori e per gli
  utilizzatori. L'inserimento nel ramo principale risolve un gran numero di
  problemi di distribuzione e di supporto.

- Nonostante gli sviluppatori kernel si sforzino di tenere stabile
  l'interfaccia dello spazio utente, quella interna al kernel è in continuo
  cambiamento. La mancanza di un'interfaccia interna è deliberatamente una
  decisione di progettazione; ciò permette che i miglioramenti fondamentali
  vengano fatti in un qualsiasi momento e che risultino fatti con un codice di
  alta qualità. Ma una delle conseguenze di questa politica è che qualsiasi
  codice "fuori dai sorgenti" richiede costante manutenzione per renderlo
  funzionante coi kernel più recenti. Tenere un codice "fuori dai sorgenti"
  richiede una mole di lavoro significativa solo per farlo funzionare.

  Invece, il codice che si trova nel ramo principale non necessita di questo
  tipo di lavoro poiché ad ogni sviluppatore che faccia una modifica alle
  interfacce viene richiesto di sistemare anche il codice che utilizza
  quell'interfaccia. Quindi, il codice che è stato inserito nel ramo principale
  ha dei costi di mantenimento significativamente più bassi.

- Oltre a ciò, spesso il codice che è all'interno del kernel sarà migliorato da
  altri sviluppatori. Dare pieni poteri alla vostra comunità di utenti e ai
  clienti può portare a sorprendenti risultati che migliorano i vostri
  prodotti.

- Il codice kernel è soggetto a revisioni, sia prima che dopo l'inserimento
  nel ramo principale.  Non importa quanto forti fossero le abilità dello
  sviluppatore originale, il processo di revisione troverà il modo di migliore
  il codice.  Spesso la revisione trova bachi importanti e problemi di
  sicurezza.  Questo è particolarmente vero per il codice che è stato
  sviluppato in un ambiente chiuso; tale codice ottiene un forte beneficio
  dalle revisioni provenienti da sviluppatori esteri. Il codice
  "fuori dai sorgenti", invece, è un codice di bassa qualità.

- La partecipazione al processo di sviluppo costituisce la vostra via per
  influenzare la direzione di sviluppo del kernel. Gli utilizzatori che
  "reclamano da bordo campo" sono ascoltati, ma gli sviluppatori attivi
  hanno una voce più forte - e la capacità di implementare modifiche che
  renderanno il kernel più funzionale alle loro necessità.

- Quando il codice è gestito separatamente, esiste sempre la possibilità che
  terze parti contribuiscano con una differente implementazione che fornisce
  le stesse funzionalità.  Se dovesse accadere, l'inserimento del codice
  diventerà molto più difficile - fino all'impossibilità.  Poi, dovrete far
  fronte a delle alternative poco piacevoli, come: (1) mantenere un elemento
  non standard "fuori dai sorgenti" per un tempo indefinito, o (2) abbandonare
  il codice e far migrare i vostri utenti alla versione "nei sorgenti".

- Contribuire al codice è l'azione fondamentale che fa funzionare tutto il
  processo. Contribuendo attraverso il vostro codice potete aggiungere nuove
  funzioni al kernel e fornire competenze ed esempi che saranno utili ad
  altri sviluppatori.  Se avete sviluppato del codice Linux (o state pensando
  di farlo), avete chiaramente interesse nel far proseguire il successo di
  questa piattaforma. Contribuire al codice è une delle migliori vie per
  aiutarne il successo.

Il ragionamento sopra citato si applica ad ogni codice "fuori dai sorgenti"
dal kernel, incluso il codice proprietario distribuito solamente in formato
binario.  Ci sono, comunque, dei fattori aggiuntivi che dovrebbero essere
tenuti in conto prima di prendere in considerazione qualsiasi tipo di
distribuzione binaria di codice kernel. Questo include che:

- Le questioni legali legate alla distribuzione di moduli kernel proprietari
  sono molto nebbiose; parecchi detentori di copyright sul kernel credono che
  molti moduli binari siano prodotti derivati del kernel e che, come risultato,
  la loro diffusione sia una violazione della licenza generale di GNU (della
  quale si parlerà più avanti).  L'autore qui non è un avvocato, e
  niente in questo documento può essere considerato come un consiglio legale.
  Il vero stato legale dei moduli proprietari può essere determinato
  esclusivamente da un giudice. Ma l'incertezza che perseguita quei moduli
  è lì comunque.

- I moduli binari aumentano di molto la difficoltà di fare debugging del
  kernel, al punto che la maggior parte degli sviluppatori del kernel non
  vorranno nemmeno tentare.  Quindi la diffusione di moduli esclusivamente
  binari renderà difficile ai vostri utilizzatori trovare un supporto dalla
  comunità.

- Il supporto è anche difficile per i distributori di moduli binari che devono
  fornire una versione del modulo per ogni distribuzione e per ogni versione
  del kernel che vogliono supportate.  Per fornire una copertura ragionevole e
  comprensiva, può essere richiesto di produrre dozzine di singoli moduli.
  E inoltre i vostri utilizzatori dovranno aggiornare il vostro modulo
  separatamente ogni volta che aggiornano il loro kernel.

- Tutto ciò che è stato detto prima riguardo alla revisione del codice si
  applica doppiamente al codice proprietario.  Dato che questo codice non è
  del tutto disponibile, non può essere revisionato dalla comunità e avrà,
  senza dubbio, seri problemi.

I produttori di sistemi integrati, in particolare, potrebbero esser tentati
dall'evitare molto di ciò che è stato detto in questa sezione, credendo che
stiano distribuendo un prodotto finito che utilizza una versione del kernel
immutabile e che non richiede un ulteriore sviluppo dopo il rilascio.  Questa
idea non comprende il valore di una vasta revisione del codice e il valore
del permettere ai propri utenti di aggiungere funzionalità al vostro prodotto.
Ma anche questi prodotti, hanno una vita commerciale limitata, dopo la quale
deve essere rilasciata una nuova versione.  A quel punto, i produttori il cui
codice è nel ramo principale di sviluppo avranno un codice ben mantenuto e
saranno in una posizione migliore per ottenere velocemente un nuovo prodotto
pronto per essere distribuito.


Licenza
-------

IL codice Linux utilizza diverse licenze, ma il codice completo deve essere
compatibile con la seconda versione della licenza GNU General Public License
(GPLv2), che è la licenza che copre la distribuzione del kernel.
Nella pratica, ciò significa che tutti i contributi al codice sono coperti
anche'essi dalla GPLv2 (con, opzionalmente, una dicitura che permette la
possibilità di distribuirlo con licenze più recenti di GPL) o dalla licenza
three-clause BSD.  Qualsiasi contributo che non è coperto da una licenza
compatibile non verrà accettata nel kernel.

Per il codice sottomesso al kernel non è necessario (o richiesto) la
concessione del Copyright.  Tutto il codice inserito nel ramo principale del
kernel conserva la sua proprietà originale; ne risulta che ora il kernel abbia
migliaia di proprietari.

Una conseguenza di questa organizzazione della proprietà è che qualsiasi
tentativo di modifica della licenza del kernel è destinata ad un quasi sicuro
fallimento.  Esistono alcuni scenari pratici nei quali il consenso di tutti
i detentori di copyright può essere ottenuto (o il loro codice verrà rimosso
dal kernel).  Quindi, in sostanza, non esiste la possibilità che si giunga ad
una versione 3 della licenza GPL nel prossimo futuro.

È imperativo che tutto il codice che contribuisce al kernel sia legittimamente
software libero.  Per questa ragione, un codice proveniente da un contributore
anonimo (o sotto pseudonimo) non verrà accettato.  È richiesto a tutti i
contributori di firmare il proprio codice, attestando così che quest'ultimo
può essere distribuito insieme al kernel sotto la licenza GPL.  Il codice che
non è stato licenziato come software libero dal proprio creatore, o che
potrebbe creare problemi di copyright per il kernel (come il codice derivante
da processi di ingegneria inversa senza le opportune tutele), non può essere
diffuso.

Domande relative a questioni legate al copyright sono frequenti nelle liste
di discussione dedicate allo sviluppo di Linux.  Tali quesiti, normalmente,
non riceveranno alcuna risposta, ma una cosa deve essere tenuta presente:
le persone che risponderanno a quelle domande non sono avvocati e non possono
fornire supporti legali.  Se avete questioni legali relative ai sorgenti
del codice Linux, non esiste alternativa che quella di parlare con un
avvocato esperto nel settore.  Fare affidamento sulle risposte ottenute da
una lista di discussione tecnica è rischioso.
