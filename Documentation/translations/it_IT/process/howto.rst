.. include:: ../disclaimer-ita.rst

:Original: :ref:`Documentation/process/howto.rst <process_howto>`
:Translator: Alessia Mantegazza <amantegazza@vaga.pv.it>

.. _it_process_howto:

Come partecipare allo sviluppo del kernel Linux
===============================================

Questo è il documento fulcro di quanto trattato sull'argomento.
Esso contiene le istruzioni su come diventare uno sviluppatore
del kernel Linux e spiega come lavorare con la comunità di
sviluppo kernel Linux. Il documento non tratterà alcun aspetto
tecnico relativo alla programmazione del kernel, ma vi aiuterà
indirizzandovi sulla corretta strada.

Se qualsiasi cosa presente in questo documento diventasse obsoleta,
vi preghiamo di inviare le correzioni agli amministratori di questo
file, indicati in fondo al presente documento.

Introduzione
------------
Dunque, volete imparare come diventare sviluppatori del kernel Linux?
O vi è stato detto dal vostro capo, "Vai, scrivi un driver Linux per
questo dispositivo". Bene, l'obbiettivo di questo documento è quello
di insegnarvi tutto ciò che dovete sapere per raggiungere il vostro
scopo descrivendo il procedimento da seguire e consigliandovi
su come lavorare con la comunità. Il documento cercherà, inoltre,
di spiegare alcune delle ragioni per le quali la comunità lavora in un
modo suo particolare.

Il kernel è scritto prevalentemente nel linguaggio C con alcune parti
specifiche dell'architettura scritte in linguaggio assembly.
Per lo sviluppo kernel è richiesta una buona conoscenza del linguaggio C.
L'assembly (di qualsiasi architettura) non è richiesto, a meno che non
pensiate di fare dello sviluppo di basso livello per un'architettura.
Sebbene essi non siano un buon sostituto ad un solido studio del
linguaggio C o ad anni di esperienza, i seguenti libri sono, se non
altro, utili riferimenti:

- "The C Programming Language" di Kernighan e Ritchie [Prentice Hall]
- "Practical C Programming" di Steve Oualline [O'Reilly]
- "C:  A Reference Manual" di Harbison and Steele [Prentice Hall]

Il kernel è stato scritto usando GNU C e la toolchain GNU.
Sebbene si attenga allo standard ISO C89, esso utilizza una serie di
estensioni che non sono previste in questo standard. Il kernel è un
ambiente C indipendente, che non ha alcuna dipendenza dalle librerie
C standard, così alcune parti del C standard non sono supportate.
Le divisioni ``long long`` e numeri in virgola mobile non sono permessi.
Qualche volta è difficile comprendere gli assunti che il kernel ha
riguardo gli strumenti e le estensioni in uso, e sfortunatamente non
esiste alcuna indicazione definitiva. Per maggiori informazioni, controllate,
la pagina `info gcc`.

Tenete a mente che state cercando di apprendere come lavorare con la comunità
di sviluppo già esistente. Questo è un gruppo eterogeneo di persone, con alti
standard di codifica, di stile e di procedura. Questi standard sono stati
creati nel corso del tempo basandosi su quanto hanno riscontrato funzionare al
meglio per un squadra così grande e geograficamente sparsa. Cercate di
imparare, in anticipo, il più possibile circa questi standard, poichè ben
spiegati; non aspettatevi che gli altri si adattino al vostro modo di fare
o a quello della vostra azienda.

Note legali
------------
Il codice sorgente del kernel Linux è rilasciato sotto GPL. Siete pregati
di visionare il file, COPYING, presente nella cartella principale dei
sorgente, per eventuali dettagli sulla licenza. Se avete ulteriori domande
sulla licenza, contattate un avvocato, non chiedete sulle liste di discussione
del kernel Linux. Le persone presenti in queste liste non sono avvocati,
e non dovreste basarvi sulle loro dichiarazioni in materia giuridica.

Per domande più frequenti e risposte sulla licenza GPL, guardare:

	https://www.gnu.org/licenses/gpl-faq.html

Documentazione
--------------
I sorgenti del kernel Linux hanno una vasta base di documenti che vi
insegneranno come interagire con la comunità del kernel. Quando nuove
funzionalità vengono aggiunte al kernel, si raccomanda di aggiungere anche i
relativi file di documentatione che spiegano come usarele.
Quando un cambiamento del kernel genera anche un cambiamento nell'interfaccia
con lo spazio utente, è raccomandabile che inviate una notifica o una
correzione alle pagine *man* spiegando tale modifica agli amministratori di
queste pagine all'indirizzo mtk.manpages@gmail.com, aggiungendo
in CC la lista linux-api@vger.kernel.org.

Di seguito una lista di file che sono presenti nei sorgente del kernel e che
è richiesto che voi leggiate:

  :ref:`Documentation/translations/it_IT/admin-guide/README.rst <it_readme>`
    Questo file da una piccola anteprima del kernel Linux e descrive il
    minimo necessario per configurare e generare il kernel. I novizi
    del kernel dovrebbero iniziare da qui.

  :ref:`Documentation/translations/it_IT/process/changes.rst <it_changes>`

    Questo file fornisce una lista dei pacchetti software necessari
    a compilare e far funzionare il kernel con successo.

  :ref:`Documentation/translations/it_IT/process/coding-style.rst <it_codingstyle>`

    Questo file descrive lo stile della codifica per il kernel Linux,
    e parte delle motivazioni che ne sono alla base. Tutto il nuovo codice deve
    seguire le linee guida in questo documento. Molti amministratori
    accetteranno patch solo se queste osserveranno tali regole, e molte
    persone revisioneranno il codice solo se scritto nello stile appropriato.

  :ref:`Documentation/translations/it_IT/process/submitting-patches.rst <it_submittingpatches>`

    Questo file descrive dettagliatamente come creare ed inviare una patch
    con successo, includendo (ma non solo questo):

       - Contenuto delle email
       - Formato delle email
       - I destinatari delle email

    Seguire tali regole non garantirà il successo (tutte le patch sono soggette
    a controlli realitivi a contenuto e stile), ma non seguirle lo precluderà
    sempre.

    Altre ottime descrizioni di come creare buone patch sono:

	"The Perfect Patch"
		https://www.ozlabs.org/~akpm/stuff/tpp.txt

	"Linux kernel patch submission format"
		https://web.archive.org/web/20180829112450/http://linux.yyz.us/patch-format.html

  :ref:`Documentation/translations/it_IT/process/stable-api-nonsense.rst <it_stable_api_nonsense>`

    Questo file descrive la motivazioni sottostanti la conscia decisione di
    non avere un API stabile all'interno del kernel, incluso cose come:

      - Sottosistemi shim-layers (per compatibilità?)
      - Portabilità fra Sistemi Operativi dei driver.
      - Attenuare i rapidi cambiamenti all'interno dei sorgenti del kernel
        (o prevenirli)

    Questo documento è vitale per la comprensione della filosifia alla base
    dello sviluppo di Linux ed è molto importante per le persone che arrivano
    da esperienze con altri Sistemi Operativi.

  :ref:`Documentation/translations/it_IT/admin-guide/security-bugs.rst <it_securitybugs>`
    Se ritenete di aver trovato un problema di sicurezza nel kernel Linux,
    seguite i passaggi scritti in questo documento per notificarlo agli
    sviluppatori del kernel, ed aiutare la risoluzione del problema.

  :ref:`Documentation/translations/it_IT/process/management-style.rst <it_managementstyle>`
    Questo documento descrive come i manutentori del kernel Linux operano
    e la filosofia comune alla base del loro metodo.  Questa è un'importante
    lettura per tutti coloro che sono nuovi allo sviluppo del kernel (o per
    chi è semplicemente curioso), poiché risolve molti dei più comuni
    fraintendimenti e confusioni dovuti al particolare comportamento dei
    manutentori del kernel.

  :ref:`Documentation/translations/it_IT/process/stable-kernel-rules.rst <it_stable_kernel_rules>`
    Questo file descrive le regole sulle quali vengono basati i rilasci del
    kernel, e spiega cosa fare se si vuole che una modifica venga inserita
    in uno di questi rilasci.

  :ref:`Documentation/translations/it_IT/process/kernel-docs.rst <it_kernel_docs>`
    Una lista di documenti pertinenti allo sviluppo del kernel.
    Per favore consultate questa lista se non trovate ciò che cercate nella
    documentazione interna del kernel.

  :ref:`Documentation/translations/it_IT/process/applying-patches.rst <it_applying_patches>`
    Una buona introduzione che descrivere esattamente cos'è una patch e come
    applicarla ai differenti rami di sviluppo del kernel.

Il kernel inoltre ha un vasto numero di documenti che possono essere
automaticamente generati dal codice sorgente stesso o da file
ReStructuredText (ReST), come questo. Esso include una completa
descrizione dell'API interna del kernel, e le regole su come gestire la
sincronizzazione (locking) correttamente

Tutte queste tipologie di documenti possono essere generati in PDF o in
HTML utilizzando::

	make pdfdocs
	make htmldocs

rispettivamente dalla cartella principale dei sorgenti del kernel.

I documenti che impiegano ReST saranno generati nella cartella
Documentation/output.
Questi posso essere generati anche in formato LaTex e ePub con::

	make latexdocs
	make epubdocs

Diventare uno sviluppatore del kernel
-------------------------------------
Se non sapete nulla sullo sviluppo del kernel Linux, dovreste dare uno
sguardo al progetto *Linux KernelNewbies*:

	https://kernelnewbies.org

Esso prevede un'utile lista di discussione dove potete porre più o meno ogni
tipo di quesito relativo ai concetti fondamentali sullo sviluppo del kernel
(assicuratevi di cercare negli archivi, prima di chiedere qualcosa alla
quale è già stata fornita risposta in passato). Esistono inoltre, un canale IRC
che potete usare per formulare domande in tempo reale, e molti documenti utili
che vi faciliteranno nell'apprendimento dello sviluppo del kernel Linux.

Il sito internet contiene informazioni di base circa l'organizzazione del
codice, sottosistemi e progetti attuali (sia interni che esterni a Linux).
Esso descrive, inoltre, informazioni logistiche di base, riguardanti ad esempio
la compilazione del kernel e l'applicazione di una modifica.

Se non sapete dove cominciare, ma volete cercare delle attività dalle quali
partire per partecipare alla comunità di sviluppo, andate al progetto Linux
Kernel Janitor's.

	https://kernelnewbies.org/KernelJanitors

È un buon posto da cui iniziare. Esso presenta una lista di problematiche
relativamente semplici da sistemare e pulire all'interno della sorgente del
kernel Linux. Lavorando con gli sviluppatori incaricati di questo progetto,
imparerete le basi per l'inserimento delle vostre modifiche all'interno dei
sorgenti del kernel Linux, e possibilmente, sarete indirizzati al lavoro
successivo da svolgere, se non ne avrete ancora idea.

Prima di apportare una qualsiasi modifica al codice del kernel Linux,
è imperativo comprendere come tale codice funziona. A questo scopo, non c'è
nulla di meglio che leggerlo direttamente (la maggior parte dei bit più
complessi sono ben commentati), eventualmente anche con l'aiuto di strumenti
specializzati. Uno degli strumenti che è particolarmente raccomandato è
il progetto Linux Cross-Reference, che è in grado di presentare codice
sorgente in un formato autoreferenziale ed indicizzato. Un eccellente ed
aggiornata fonte di consultazione del codice del kernel la potete trovare qui:

	https://elixir.bootlin.com/


Il processo di sviluppo
-----------------------
Il processo di sviluppo del kernel Linux si compone di pochi "rami" principali
e di molti altri rami per specifici sottosistemi. Questi rami sono:

  - I sorgenti kernel 4.x
  - I sorgenti stabili del kernel 4.x.y -stable
  - Sorgenti dei sottosistemi del kernel e le loro modifiche
  - Il kernel 4.x -next per test d'integrazione

I sorgenti kernel 4.x
~~~~~~~~~~~~~~~~~~~~~

I kernel 4.x sono amministrati da Linus Torvald, e possono essere trovati
su https://kernel.org nella cartella pub/linux/kernel/v4.x/. Il processo
di sviluppo è il seguente:

  - Non appena un nuovo kernel viene rilasciato si apre una finestra di due
    settimane. Durante questo periodo i manutentori possono proporre a Linus
    dei grossi cambiamenti; solitamente i cambiamenti che sono già stati
    inseriti nel ramo -next del kernel per alcune settimane. Il modo migliore
    per sottoporre dei cambiamenti è attraverso git (lo strumento usato per
    gestire i sorgenti del kernel, più informazioni sul sito
    https://git-scm.com/) ma anche delle patch vanno bene.

  - Al termine delle due settimane un kernel -rc1 viene rilasciato e
    l'obbiettivo ora è quello di renderlo il più solido possibile. A questo
    punto la maggior parte delle patch dovrebbero correggere un'eventuale
    regressione. I bachi che sono sempre esistiti non sono considerabili come
    regressioni, quindi inviate questo tipo di cambiamenti solo se sono
    importanti. Notate che un intero driver (o filesystem) potrebbe essere
    accettato dopo la -rc1 poiché non esistono rischi di una possibile
    regressione con tale cambiamento, fintanto che quest'ultimo è
    auto-contenuto e non influisce su aree esterne al codice che è stato
    aggiunto. git può essere utilizzato per inviare le patch a Linus dopo che
    la -rc1 è stata rilasciata, ma è anche necessario inviare le patch ad
    una lista di discussione pubblica per un'ulteriore revisione.

  - Una nuova -rc viene rilasciata ogni volta che Linus reputa che gli attuali
    sorgenti siano in uno stato di salute ragionevolmente adeguato ai test.
    L'obiettivo è quello di rilasciare una nuova -rc ogni settimana.

  - Il processo continua fino a che il kernel è considerato "pronto"; tale
    processo dovrebbe durare circa in 6 settimane.

È utile menzionare quanto scritto da Andrew Morton sulla lista di discussione
kernel-linux in merito ai rilasci del kernel:

	*"Nessuno sa quando un kernel verrà rilasciato, poichè questo è
	legato allo stato dei bachi e non ad una cronologia preventiva."*

I sorgenti stabili del kernel 4.x.y -stable
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

I kernel con versioni in 3-parti sono "kernel stabili". Essi contengono
correzioni critiche relativamente piccole nell'ambito della sicurezza
oppure significative regressioni scoperte in un dato 4.x kernel.

Questo è il ramo raccomandato per gli utenti che vogliono un kernel recente
e stabile e non sono interessati a dare il proprio contributo alla verifica
delle versioni di sviluppo o sperimentali.

Se non è disponibile alcun kernel 4.x.y., quello più aggiornato e stabile
sarà il kernel 4.x con la numerazione più alta.

4.x.y sono amministrati dal gruppo "stable" <stable@vger.kernel.org>, e sono
rilasciati a seconda delle esigenze. Il normale periodo di rilascio è
approssimativamente di due settimane, ma può essere più lungo se non si
verificano problematiche urgenti. Un problema relativo alla sicurezza, invece,
può determinare un rilascio immediato.

Il file Documentation/process/stable-kernel-rules.rst (nei sorgenti) documenta
quali tipologie di modifiche sono accettate per i sorgenti -stable, e come
avviene il processo di rilascio.


Sorgenti dei sottosistemi del kernel e le loro patch
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

I manutentori dei diversi sottosistemi del kernel --- ed anche molti
sviluppatori di sottosistemi --- mostrano il loro attuale stato di sviluppo
nei loro repositori. In questo modo, altri possono vedere cosa succede nelle
diverse parti del kernel. In aree dove lo sviluppo è rapido, potrebbe essere
chiesto ad uno sviluppatore di basare le proprie modifiche su questi repositori
in modo da evitare i conflitti fra le sottomissioni ed altri lavori in corso

La maggior parte di questi repositori sono git, ma esistono anche altri SCM
in uso, o file di patch pubblicate come una serie quilt.
Gli indirizzi dei repositori di sottosistema sono indicati nel file
MAINTAINERS.  Molti di questi posso essere trovati su  https://git.kernel.org/.

Prima che una modifica venga inclusa in questi sottosistemi, sarà soggetta ad
una revisione che inizialmente avviene tramite liste di discussione (vedere la
sezione dedicata qui sotto). Per molti sottosistemi del kernel, tale processo
di revisione è monitorato con lo strumento patchwork.
Patchwork offre un'interfaccia web che mostra le patch pubblicate, inclusi i
commenti o le revisioni fatte, e gli amministratori possono indicare le patch
come "in revisione", "accettate", o "rifiutate". Diversi siti Patchwork sono
elencati al sito https://patchwork.kernel.org/.

Il kernel 4.x -next per test d'integrazione
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Prima che gli aggiornamenti dei sottosistemi siano accorpati nel ramo
principale 4.x, sarà necessario un test d'integrazione.
A tale scopo, esiste un repositorio speciale di test nel quale virtualmente
tutti i rami dei sottosistemi vengono inclusi su base quotidiana:

	https://git.kernel.org/?p=linux/kernel/git/next/linux-next.git

In questo modo, i kernel -next offrono uno sguardo riassuntivo su quello che
ci si aspetterà essere nel kernel principale nel successivo periodo
d'incorporazione.
Coloro che vorranno fare dei test d'esecuzione del kernel -next sono più che
benvenuti.


Riportare Bug
-------------

Il file 'Documentation/admin-guide/reporting-issues.rst' nella
cartella principale del kernel spiega come segnalare un baco nel
kernel, e fornisce dettagli su quali informazioni sono necessarie agli
sviluppatori del kernel per poter studiare il problema.

Gestire i rapporti sui bug
--------------------------

Uno dei modi migliori per mettere in pratica le vostre capacità di hacking è
quello di riparare bachi riportati da altre persone. Non solo aiuterete a far
diventare il kernel più stabile, ma imparerete a riparare problemi veri dal
mondo ed accrescerete le vostre competenze, e gli altri sviluppatori saranno
al corrente della vostra presenza. Riparare bachi è una delle migliori vie per
acquisire meriti tra gli altri sviluppatori, perchè non a molte persone piace
perdere tempo a sistemare i bachi di altri.

Per lavorare sui bachi già segnalati, per prima cosa cercate il
sottosistema che vi interessa. Poi, verificate nel file MAINTAINERS
dove vengono collezionati solitamente i bachi per quel sottosistema;
spesso sarà una lista di discussione, raramente un bugtracker. Cercate
bachi nell'archivio e aiutate dove credete di poterlo fare. Potete
anche consultare https://bugzilla.kernel.org; però, solo una manciata di
sottosistemi lo usano attivamente, ciò nonostante i bachi che
coinvolgono l'intero kernel sono sempre riportati lì.

Liste di discussione
--------------------

Come descritto in molti dei documenti qui sopra, la maggior parte degli
sviluppatori del kernel partecipano alla lista di discussione Linux Kernel.
I dettagli su come iscriversi e disiscriversi dalla lista possono essere
trovati al sito:

	http://vger.kernel.org/vger-lists.html#linux-kernel

Ci sono diversi archivi della lista di discussione. Usate un qualsiasi motore
di ricerca per trovarli. Per esempio:

	https://lore.kernel.org/lkml/

É caldamente consigliata una ricerca in questi archivi sul tema che volete
sollevare, prima di pubblicarlo sulla lista. Molte cose sono già state
discusse in dettaglio e registrate negli archivi della lista di discussione.

Molti dei sottosistemi del kernel hanno anche una loro lista di discussione
dedicata.  Guardate nel file MAINTAINERS per avere una lista delle liste di
discussione e il loro uso.

Molte di queste liste sono gestite su kernel.org. Per informazioni consultate
la seguente pagina:

	http://vger.kernel.org/vger-lists.html

Per favore ricordatevi della buona educazione quando utilizzate queste liste.
Sebbene sia un pò dozzinale, il seguente URL contiene alcune semplici linee
guida per interagire con la lista (o con qualsiasi altra lista):

	http://www.albion.com/netiquette/

Se diverse persone rispondo alla vostra mail, la lista dei riceventi (copia
conoscenza) potrebbe diventare abbastanza lunga. Non cancellate nessuno dalla
lista di CC: senza un buon motivo, e non rispondete solo all'indirizzo
della lista di discussione. Fateci l'abitudine perché capita spesso di
ricevere la stessa email due volte: una dal mittente ed una dalla lista; e non
cercate di modificarla aggiungendo intestazioni stravaganti, agli altri non
piacerà.

Ricordate di rimanere sempre in argomento e di mantenere le attribuzioni
delle vostre risposte invariate; mantenete il "John Kernelhacker wrote ...:"
in cima alla vostra replica e aggiungete le vostre risposte fra i singoli
blocchi citati, non scrivete all'inizio dell'email.

Se aggiungete patch alla vostra mail, assicuratevi che siano del tutto
leggibili come indicato in Documentation/process/submitting-patches.rst.
Gli sviluppatori kernel non vogliono avere a che fare con allegati o patch
compresse; vogliono invece poter commentare le righe dei vostri cambiamenti,
il che può funzionare solo in questo modo.
Assicuratevi di utilizzare un gestore di mail che non alterì gli spazi ed i
caratteri. Un ottimo primo test è quello di inviare a voi stessi una mail e
cercare di sottoporre la vostra stessa patch. Se non funziona, sistemate il
vostro programma di posta, o cambiatelo, finché non funziona.

Ed infine, per favore ricordatevi di mostrare rispetto per gli altri
sottoscriventi.

Lavorare con la comunità
------------------------

L'obiettivo di questa comunità è quello di fornire il miglior kernel possibile.
Quando inviate una modifica che volete integrare, sarà valutata esclusivamente
dal punto di vista tecnico. Quindi, cosa dovreste aspettarvi?

  - critiche
  - commenti
  - richieste di cambiamento
  - richieste di spiegazioni
  - nulla

Ricordatevi che questo fa parte dell'integrazione della vostra modifica
all'interno del kernel.  Dovete essere in grado di accettare le critiche,
valutarle a livello tecnico ed eventualmente rielaborare nuovamente le vostre
modifiche o fornire delle chiare e concise motivazioni per le quali le
modifiche suggerite non dovrebbero essere fatte.
Se non riceverete risposte, aspettate qualche giorno e riprovate ancora,
qualche volta le cose si perdono nell'enorme mucchio di email.

Cosa non dovreste fare?

  - aspettarvi che la vostra modifica venga accettata senza problemi
  - mettervi sulla difensiva
  - ignorare i commenti
  - sottomettere nuovamente la modifica senza fare nessuno dei cambiamenti
    richiesti

In una comunità che è alla ricerca delle migliori soluzioni tecniche possibili,
ci saranno sempre opinioni differenti sull'utilità di una modifica.
Siate cooperativi e vogliate adattare la vostra idea in modo che sia inserita
nel kernel.  O almeno vogliate dimostrare che la vostra idea vale.
Ricordatevi, sbagliare è accettato fintanto che siate disposti a lavorare verso
una soluzione che è corretta.

È normale che le risposte alla vostra prima modifica possa essere
semplicemente una lista con dozzine di cose che dovreste correggere.
Questo **non** implica che la vostra patch non sarà accettata, e questo
**non** è contro di voi personalmente.
Semplicemente correggete tutte le questioni sollevate contro la vostra modifica
ed inviatela nuovamente.

Differenze tra la comunità del kernel e le strutture aziendali
--------------------------------------------------------------

La comunità del kernel funziona diversamente rispetto a molti ambienti di
sviluppo aziendali.  Qui di seguito una lista di cose che potete provare a
fare per evitare problemi:

  Cose da dire riguardanti le modifiche da voi proposte:

  - "Questo risolve più problematiche."
  - "Questo elimina 2000 stringhe di codice."
  - "Qui una modifica che spiega cosa sto cercando di fare."
  - "L'ho testato su 5 diverse architetture.."
  - "Qui una serie di piccole modifiche che.."
  - "Questo aumenta le prestazioni di macchine standard..."

 Cose che dovreste evitare di dire:

    - "Lo abbiamo fatto in questo modo in AIX/ptx/Solaris, di conseguenza
       deve per forza essere giusto..."
    - "Ho fatto questo per 20 anni, quindi.."
    - "Questo è richiesto dalla mia Azienda per far soldi"
    - "Questo è per la linea di prodotti della nostra Azienda"
    - "Ecco il mio documento di design di 1000 pagine che descrive ciò che ho
       in mente"
    - "Ci ho lavorato per 6 mesi..."
    - "Ecco una patch da 5000 righe che.."
    - "Ho riscritto il pasticcio attuale, ed ecco qua.."
    - "Ho una scadenza, e questa modifica ha bisogno di essere approvata ora"

Un'altra cosa nella quale la comunità del kernel si differenzia dai più
classici ambienti di ingegneria del software è la natura "senza volto" delle
interazioni umane. Uno dei benefici dell'uso delle email e di irc come forma
primordiale di comunicazione è l'assenza di discriminazione basata su genere e
razza. L'ambienti di lavoro Linux accetta donne e minoranze perchè tutto quello
che sei è un indirizzo email.  Aiuta anche l'aspetto internazionale nel
livellare il terreno di gioco perchè non è possibile indovinare il genere
basandosi sul nome di una persona. Un uomo può chiamarsi Andrea ed una donna
potrebbe chiamarsi Pat. Gran parte delle donne che hanno lavorato al kernel
Linux e che hanno espresso una personale opinione hanno avuto esperienze
positive.

La lingua potrebbe essere un ostacolo per quelle persone che non si trovano
a loro agio con l'inglese.  Una buona padronanza del linguaggio può essere
necessaria per esporre le proprie idee in maniera appropiata all'interno
delle liste di discussione, quindi è consigliabile che rileggiate le vostre
email prima di inviarle in modo da essere certi che abbiano senso in inglese.


Spezzare le vostre modifiche
----------------------------

La comunità del kernel Linux non accetta con piacere grossi pezzi di codice
buttati lì tutti in una volta. Le modifiche necessitano di essere
adeguatamente presentate, discusse, e suddivise in parti più piccole ed
indipendenti.  Questo è praticamente l'esatto opposto di quello che le
aziende fanno solitamente.  La vostra proposta dovrebbe, inoltre, essere
presentata prestissimo nel processo di sviluppo, così che possiate ricevere
un riscontro su quello che state facendo. Lasciate che la comunità
senta che state lavorando con loro, e che non li stiate sfruttando come
discarica per le vostre aggiunte.  In ogni caso, non inviate 50 email nello
stesso momento in una lista di discussione, il più delle volte la vostra serie
di modifiche dovrebbe essere più piccola.

I motivi per i quali dovreste frammentare le cose sono i seguenti:

1) Piccole modifiche aumentano le probabilità che vengano accettate,
   altrimenti richiederebbe troppo tempo o sforzo nel verificarne
   la correttezza.  Una modifica di 5 righe può essere accettata da un
   manutentore con a mala pena una seconda occhiata. Invece, una modifica da
   500 linee può richiedere ore di rilettura per verificarne la correttezza
   (il tempo necessario è esponenzialmente proporzionale alla dimensione della
   modifica, o giù di lì)

   Piccole modifiche sono inoltre molto facili da debuggare quando qualcosa
   non va. È molto più facile annullare le modifiche una per una che
   dissezionare una patch molto grande dopo la sua sottomissione (e rompere
   qualcosa).

2) È importante non solo inviare piccole modifiche, ma anche riscriverle e
   semplificarle (o più semplicemente ordinarle) prima di sottoporle.

Qui un'analogia dello sviluppatore kernel Al Viro:

	*"Pensate ad un insegnante di matematica che corregge il compito
	di uno studente (di matematica). L'insegnante non vuole vedere le
	prove e gli errori commessi dallo studente prima che arrivi alla
	soluzione. Vuole vedere la risposta più pulita ed elegante
	possibile.  Un buono studente lo sa, e non presenterebbe mai le
	proprie bozze prima prima della soluzione finale"*

	*"Lo stesso vale per lo sviluppo del kernel. I manutentori ed i
	revisori non vogliono vedere il procedimento che sta dietro al
	problema che uno sta risolvendo. Vogliono vedere una soluzione
	semplice ed elegante."*

Può essere una vera sfida il saper mantenere l'equilibrio fra una presentazione
elegante della vostra soluzione, lavorare insieme ad una comunità e dibattere
su un lavoro incompleto.  Pertanto è bene entrare presto nel processo di
revisione per migliorare il vostro lavoro, ma anche per riuscire a tenere le
vostre modifiche in pezzettini che potrebbero essere già accettate, nonostante
la vostra intera attività non lo sia ancora.

In fine, rendetevi conto che non è accettabile inviare delle modifiche
incomplete con la promessa che saranno "sistemate dopo".


Giustificare le vostre modifiche
--------------------------------

Insieme alla frammentazione delle vostre modifiche, è altrettanto importante
permettere alla comunità Linux di capire perché dovrebbero accettarle.
Nuove funzionalità devono essere motivate come necessarie ed utili.


Documentare le vostre modifiche
-------------------------------

Quando inviate le vostre modifiche, fate particolare attenzione a quello che
scrivete nella vostra email.  Questa diventerà il *ChangeLog* per la modifica,
e sarà visibile a tutti per sempre.  Dovrebbe descrivere la modifica nella sua
interezza, contenendo:

 - perchè la modifica è necessaria
 - l'approccio d'insieme alla patch
 - dettagli supplementari
 - risultati dei test

Per maggiori dettagli su come tutto ciò dovrebbe apparire, riferitevi alla
sezione ChangeLog del documento:

 "The Perfect Patch"
      http://www.ozlabs.org/~akpm/stuff/tpp.txt

A volte tutto questo è difficile da realizzare. Il perfezionamento di queste
pratiche può richiedere anni (eventualmente). È un processo continuo di
miglioramento che richiede molta pazienza e determinazione. Ma non mollate,
si può fare. Molti lo hanno fatto prima, ed ognuno ha dovuto iniziare dove
siete voi ora.




----------

Grazie a Paolo Ciarrocchi che ha permesso che la sezione "Development Process"
(https://lwn.net/Articles/94386/) fosse basata sui testi da lui scritti, ed a
Randy Dunlap e Gerrit Huizenga per la lista di cose che dovreste e non
dovreste dire. Grazie anche a Pat Mochel, Hanna Linder, Randy Dunlap,
Kay Sievers, Vojtech Pavlik, Jan Kara, Josh Boyer, Kees Cook, Andrew Morton,
Andi Kleen, Vadim Lobanov, Jesper Juhl, Adrian Bunk, Keri Harris, Frans Pop,
David A. Wheeler, Junio Hamano, Michael Kerrisk, e Alex Shepard per le
loro revisioni, commenti e contributi.  Senza il loro aiuto, questo documento
non sarebbe stato possibile.

Manutentore: Greg Kroah-Hartman <greg@kroah.com>
