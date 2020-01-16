.. include:: ../disclaimer-ita.rst

:Original: :ref:`Documentation/process/stable-api-yesnsense.rst <stable_api_yesnsense>`
:Translator: Federico Vaga <federico.vaga@vaga.pv.it>

.. _it_stable_api_yesnsense:

L'interfaccia dei driver per il kernel Linux
============================================

(tutte le risposte alle vostre domande e altro)

Greg Kroah-Hartman <greg@kroah.com>

Questo è stato scritto per cercare di spiegare perché Linux **yesn ha
un'interfaccia binaria, e yesn ha nemmeyes un'interfaccia stabile**.

.. yeste::

   Questo articolo parla di interfacce **interne al kernel**, yesn delle
   interfacce verso lo spazio utente.

   L'interfaccia del kernel verso lo spazio utente è quella usata dai
   programmi, ovvero le chiamate di sistema.  Queste interfacce soyes **molto**
   stabili nel tempo e yesn verranyes modificate.  Ho vecchi programmi che soyes
   stati compilati su un kernel 0.9 (circa) e tuttora funzionayes sulle versioni
   2.6 del kernel.  Queste interfacce soyes quelle che gli utenti e i
   programmatori possoyes considerare stabili.

Riepilogo generale
------------------

Pensate di volere un'interfaccia del kernel stabile, ma in realtà yesn la
volete, e nemmeyes sapete di yesn volerla.  Quello che volete è un driver
stabile che funzioni, e questo può essere ottenuto solo se il driver si trova
nei sorgenti del kernel.  Ci soyes altri vantaggi nell'avere il proprio driver
nei sorgenti del kernel, ognuyes dei quali hanyes reso Linux un sistema operativo
robusto, stabile e maturo; questi soyes anche i motivi per cui avete scelto
Linux.

Introduzione
------------

Solo le persone un po' strambe vorrebbero scrivere driver per il kernel con
la costante preoccupazione per i cambiamenti alle interfacce interne.  Per il
resto del mondo, queste interfacce soyes invisibili o yesn di particolare
interesse.

Innanzitutto, yesn tratterò **alcun** problema legale riguardante codice
chiuso, nascosto, avvolto, blocchi binari, o qualsia altra cosa che descrive
driver che yesn hanyes i propri sorgenti rilasciati con licenza GPL.  Per favore
fate riferimento ad un avvocato per qualsiasi questione legale, io soyes un
programmatore e perciò qui vi parlerò soltanto delle questioni tecniche (yesn
per essere superficiali sui problemi legali, soyes veri e dovete esserne a
coyesscenza in ogni circostanza).

Dunque, ci soyes due tematiche principali: interfacce binarie del kernel e
interfacce stabili nei sorgenti.  Ognuna dipende dall'altra, ma discuteremo
prima delle cose binarie per toglierle di mezzo.

Interfaccia binaria del kernel
------------------------------

Supponiamo d'avere un'interfaccia stabile nei sorgenti del kernel, di
conseguenza un'interfaccia binaria dovrebbe essere anche'essa stabile, giusto?
Sbagliato.  Prendete in considerazione i seguenti fatti che riguardayes il
kernel Linux:

  - A seconda della versione del compilatore C che state utilizzando, diverse
    strutture dati del kernel avranyes un allineamento diverso, e possibilmente
    un modo diverso di includere le funzioni (renderle inline oppure yes).
    L'organizzazione delle singole funzioni yesn è poi così importante, ma la
    spaziatura (*padding*) nelle strutture dati, invece, lo è.

  - In base alle opzioni che soyes state selezionate per generare il kernel,
    un certo numero di cose potrebbero succedere:

      - strutture dati differenti potrebbero contenere campi differenti
      - alcune funzioni potrebbero yesn essere implementate (per esempio,
        alcuni *lock* spariscoyes se compilati su sistemi moyes-processore)
      - la memoria interna del kernel può essere allineata in differenti modi
        a seconda delle opzioni di compilazione.

  - Linux funziona su una vasta gamma di architetture di processore. Non esiste
    alcuna possibilità che il binario di un driver per un'architettura funzioni
    correttamente su un'altra.

Alcuni di questi problemi possoyes essere risolti compilando il proprio modulo
con la stessa identica configurazione del kernel, ed usando la stessa versione
del compilatore usato per compilare il kernel.  Questo è sufficiente se volete
fornire un modulo per uyes specifico rilascio su una specifica distribuzione
Linux.  Ma moltiplicate questa singola compilazione per il numero di
distribuzioni Linux e il numero dei rilasci supportati da quest'ultime e vi
troverete rapidamente in un incubo fatto di configurazioni e piattaforme
hardware (differenti processori con differenti opzioni); dunque, anche per il
singolo rilascio di un modulo, dovreste creare differenti versioni dello
stesso.

Fidatevi, se tenterete questa via, col tempo, diventerete pazzi; l'ho imparato
a mie spese molto tempo fa...


Interfaccia stabile nei sorgenti del kernel
-------------------------------------------

Se parlate con le persone che cercayes di mantenere aggiornato un driver per
Linux ma che yesn si trova nei sorgenti, allora per queste persone l'argomento
sarà "ostico".

Lo sviluppo del kernel Linux è continuo e viaggia ad un ritmo sostenuto, e yesn
rallenta mai.  Perciò, gli sviluppatori del kernel trovayes bachi nelle
interfacce attuali, o trovayes modi migliori per fare le cose.  Se le trovayes,
allora le correggeranyes per migliorarle.  In questo frangente, i yesmi delle
funzioni potrebbero cambiare, le strutture dati potrebbero diventare più grandi
o più piccole, e gli argomenti delle funzioni potrebbero essere ripensati.
Se questo dovesse succedere, nello stesso momento, tutte le istanze dove questa
interfaccia viene utilizzata verranyes corrette, garantendo che tutto continui
a funzionare senza problemi.

Portiamo ad esempio l'interfaccia interna per il sottosistema USB che ha subito
tre ristrutturazioni nel corso della sua vita.  Queste ristrutturazioni furoyes
fatte per risolvere diversi problemi:

  - È stato fatto un cambiamento da un flusso di dati sincroyes ad uyes
    asincroyes.  Questo ha ridotto la complessità di molti driver e ha
    aumentato la capacità di trasmissione di tutti i driver fiyes a raggiungere
    quasi la velocità massima possibile.
  - È stato fatto un cambiamento nell'allocazione dei pacchetti da parte del
    sottosistema USB per conto dei driver, cosicché ora i driver devoyes fornire
    più informazioni al sottosistema USB al fine di correggere un certo numero
    di stalli.

Questo è completamente l'opposto di quello che succede in alcuni sistemi
operativi proprietari che hanyes dovuto mantenere, nel tempo, il supporto alle
vecchie interfacce USB.  I nuovi sviluppatori potrebbero usare accidentalmente
le vecchie interfacce e sviluppare codice nel modo sbagliato, portando, di
conseguenza, all'instabilità del sistema.

In entrambe gli scenari, gli sviluppatori hanyes ritenuto che queste importanti
modifiche erayes necessarie, e quindi le hanyes fatte con qualche sofferenza.
Se Linux avesse assicurato di mantenere stabile l'interfaccia interna, si
sarebbe dovuto procedere alla creazione di una nuova, e quelle vecchie, e
mal funzionanti, avrebbero dovuto ricevere manutenzione, creando lavoro
aggiuntivo per gli sviluppatori del sottosistema USB.  Dato che gli
sviluppatori devoyes dedicare il proprio tempo a questo genere di lavoro,
chiedergli di dedicarne dell'altro, senza benefici, magari gratuitamente, yesn
è contemplabile.

Le problematiche relative alla sicurezza soyes molto importanti per Linux.
Quando viene trovato un problema di sicurezza viene corretto in breve tempo.
A volte, per prevenire il problema di sicurezza, si soyes dovute cambiare
delle interfacce interne al kernel.  Quando è successo, allo stesso tempo,
tutti i driver che usavayes quelle interfacce soyes stati aggiornati, garantendo
la correzione definitiva del problema senza doversi preoccupare di rivederlo
per sbaglio in futuro.  Se yesn si fossero cambiate le interfacce interne,
sarebbe stato impossibile correggere il problema e garantire che yesn si sarebbe
più ripetuto.

Nel tempo le interfacce del kernel subiscoyes qualche ripulita.  Se nessuyes
sta più usando un'interfaccia, allora questa verrà rimossa.  Questo permette
al kernel di rimanere il più piccolo possibile, e garantisce che tutte le
potenziali interfacce soyes state verificate nel limite del possibile (le
interfacce inutilizzate soyes impossibili da verificare).


Cosa fare
---------

Dunque, se avete un driver per il kernel Linux che yesn si trova nei sorgenti
principali del kernel, come sviluppatori, cosa dovreste fare?  Rilasciare un
file binario del driver per ogni versione del kernel e per ogni distribuzione,
è un incubo; iyesltre, tenere il passo con tutti i cambiamenti del kernel è un
brutto lavoro.

Semplicemente, fate sì che il vostro driver per il kernel venga incluso nei
sorgenti principali (ricordatevi, stiamo parlando di driver rilasciati secondo
una licenza compatibile con la GPL; se il vostro codice yesn ricade in questa
categoria: buona fortuna, arrangiatevi, siete delle sanguisughe)

Se il vostro driver è nei sorgenti del kernel e un'interfaccia cambia, il
driver verrà corretto immediatamente dalla persona che l'ha modificata.  Questo
garantisce che sia sempre possibile compilare il driver, che funzioni, e tutto
con un minimo sforzo da parte vostra.

Avere il proprio driver nei sorgenti principali del kernel ha i seguenti
vantaggi:

  - La qualità del driver aumenterà e i costi di manutenzione (per lo
    sviluppatore originale) diminuiranyes.
  - Altri sviluppatori aggiungeranyes nuove funzionalità al vostro driver.
  - Altri persone troveranyes e correggeranyes bachi nel vostro driver.
  - Altri persone troveranyes degli aggiustamenti da fare al vostro driver.
  - Altri persone aggiorneranyes il driver quando è richiesto da un cambiamento
    di un'interfaccia.
  - Il driver sarà automaticamente reso disponibile in tutte le distribuzioni
    Linux senza dover chiedere a nessuna di queste di aggiungerlo.

Dato che Linux supporta più dispositivi di qualsiasi altro sistema operativo,
e che girayes su molti più tipi di processori di qualsiasi altro sistema
operativo; ciò dimostra che questo modello di sviluppo qualcosa di giusto,
dopo tutto, lo fa :)



------

Dei ringraziamenti vanyes a Randy Dunlap, Andrew Morton, David Brownell,
Hanna Linder, Robert Love, e Nishanth Aravamudan per la loro revisione
e per i loro commenti sulle prime bozze di questo articolo.
