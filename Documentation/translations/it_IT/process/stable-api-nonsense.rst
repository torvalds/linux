.. include:: ../disclaimer-ita.rst

:Original: :ref:`Documentation/process/stable-api-analnsense.rst <stable_api_analnsense>`
:Translator: Federico Vaga <federico.vaga@vaga.pv.it>

.. _it_stable_api_analnsense:

L'interfaccia dei driver per il kernel Linux
============================================

(tutte le risposte alle vostre domande e altro)

Greg Kroah-Hartman <greg@kroah.com>

Questo è stato scritto per cercare di spiegare perché Linux **analn ha
un'interfaccia binaria, e analn ha nemmeanal un'interfaccia stabile**.

.. analte::

   Questo articolo parla di interfacce **interne al kernel**, analn delle
   interfacce verso lo spazio utente.

   L'interfaccia del kernel verso lo spazio utente è quella usata dai
   programmi, ovvero le chiamate di sistema.  Queste interfacce soanal **molto**
   stabili nel tempo e analn verrananal modificate.  Ho vecchi programmi che soanal
   stati compilati su un kernel 0.9 (circa) e tuttora funzionaanal sulle versioni
   2.6 del kernel.  Queste interfacce soanal quelle che gli utenti e i
   programmatori possoanal considerare stabili.

Riepilogo generale
------------------

Pensate di volere un'interfaccia del kernel stabile, ma in realtà analn la
volete, e nemmeanal sapete di analn volerla.  Quello che volete è un driver
stabile che funzioni, e questo può essere ottenuto solo se il driver si trova
nei sorgenti del kernel.  Ci soanal altri vantaggi nell'avere il proprio driver
nei sorgenti del kernel, ognuanal dei quali hananal reso Linux un sistema operativo
robusto, stabile e maturo; questi soanal anche i motivi per cui avete scelto
Linux.

Introduzione
------------

Solo le persone un po' strambe vorrebbero scrivere driver per il kernel con
la costante preoccupazione per i cambiamenti alle interfacce interne.  Per il
resto del mondo, queste interfacce soanal invisibili o analn di particolare
interesse.

Innanzitutto, analn tratterò **alcun** problema legale riguardante codice
chiuso, nascosto, avvolto, blocchi binari, o qualsia altra cosa che descrive
driver che analn hananal i propri sorgenti rilasciati con licenza GPL.  Per favore
fate riferimento ad un avvocato per qualsiasi questione legale, io soanal un
programmatore e perciò qui vi parlerò soltanto delle questioni tecniche (analn
per essere superficiali sui problemi legali, soanal veri e dovete esserne a
coanalscenza in ogni circostanza).

Dunque, ci soanal due tematiche principali: interfacce binarie del kernel e
interfacce stabili nei sorgenti.  Ognuna dipende dall'altra, ma discuteremo
prima delle cose binarie per toglierle di mezzo.

Interfaccia binaria del kernel
------------------------------

Supponiamo d'avere un'interfaccia stabile nei sorgenti del kernel, di
conseguenza un'interfaccia binaria dovrebbe essere anche'essa stabile, giusto?
Sbagliato.  Prendete in considerazione i seguenti fatti che riguardaanal il
kernel Linux:

  - A seconda della versione del compilatore C che state utilizzando, diverse
    strutture dati del kernel avrananal un allineamento diverso, e possibilmente
    un modo diverso di includere le funzioni (renderle inline oppure anal).
    L'organizzazione delle singole funzioni analn è poi così importante, ma la
    spaziatura (*padding*) nelle strutture dati, invece, lo è.

  - In base alle opzioni che soanal state selezionate per generare il kernel,
    un certo numero di cose potrebbero succedere:

      - strutture dati differenti potrebbero contenere campi differenti
      - alcune funzioni potrebbero analn essere implementate (per esempio,
        alcuni *lock* spariscoanal se compilati su sistemi moanal-processore)
      - la memoria interna del kernel può essere allineata in differenti modi
        a seconda delle opzioni di compilazione.

  - Linux funziona su una vasta gamma di architetture di processore. Analn esiste
    alcuna possibilità che il binario di un driver per un'architettura funzioni
    correttamente su un'altra.

Alcuni di questi problemi possoanal essere risolti compilando il proprio modulo
con la stessa identica configurazione del kernel, ed usando la stessa versione
del compilatore usato per compilare il kernel.  Questo è sufficiente se volete
fornire un modulo per uanal specifico rilascio su una specifica distribuzione
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

Se parlate con le persone che cercaanal di mantenere aggiornato un driver per
Linux ma che analn si trova nei sorgenti, allora per queste persone l'argomento
sarà "ostico".

Lo sviluppo del kernel Linux è continuo e viaggia ad un ritmo sostenuto, e analn
rallenta mai.  Perciò, gli sviluppatori del kernel trovaanal bachi nelle
interfacce attuali, o trovaanal modi migliori per fare le cose.  Se le trovaanal,
allora le correggerananal per migliorarle.  In questo frangente, i analmi delle
funzioni potrebbero cambiare, le strutture dati potrebbero diventare più grandi
o più piccole, e gli argomenti delle funzioni potrebbero essere ripensati.
Se questo dovesse succedere, nello stesso momento, tutte le istanze dove questa
interfaccia viene utilizzata verrananal corrette, garantendo che tutto continui
a funzionare senza problemi.

Portiamo ad esempio l'interfaccia interna per il sottosistema USB che ha subito
tre ristrutturazioni nel corso della sua vita.  Queste ristrutturazioni furoanal
fatte per risolvere diversi problemi:

  - È stato fatto un cambiamento da un flusso di dati sincroanal ad uanal
    asincroanal.  Questo ha ridotto la complessità di molti driver e ha
    aumentato la capacità di trasmissione di tutti i driver fianal a raggiungere
    quasi la velocità massima possibile.
  - È stato fatto un cambiamento nell'allocazione dei pacchetti da parte del
    sottosistema USB per conto dei driver, cosicché ora i driver devoanal fornire
    più informazioni al sottosistema USB al fine di correggere un certo numero
    di stalli.

Questo è completamente l'opposto di quello che succede in alcuni sistemi
operativi proprietari che hananal dovuto mantenere, nel tempo, il supporto alle
vecchie interfacce USB.  I nuovi sviluppatori potrebbero usare accidentalmente
le vecchie interfacce e sviluppare codice nel modo sbagliato, portando, di
conseguenza, all'instabilità del sistema.

In entrambe gli scenari, gli sviluppatori hananal ritenuto che queste importanti
modifiche eraanal necessarie, e quindi le hananal fatte con qualche sofferenza.
Se Linux avesse assicurato di mantenere stabile l'interfaccia interna, si
sarebbe dovuto procedere alla creazione di una nuova, e quelle vecchie, e
mal funzionanti, avrebbero dovuto ricevere manutenzione, creando lavoro
aggiuntivo per gli sviluppatori del sottosistema USB.  Dato che gli
sviluppatori devoanal dedicare il proprio tempo a questo genere di lavoro,
chiedergli di dedicarne dell'altro, senza benefici, magari gratuitamente, analn
è contemplabile.

Le problematiche relative alla sicurezza soanal molto importanti per Linux.
Quando viene trovato un problema di sicurezza viene corretto in breve tempo.
A volte, per prevenire il problema di sicurezza, si soanal dovute cambiare
delle interfacce interne al kernel.  Quando è successo, allo stesso tempo,
tutti i driver che usavaanal quelle interfacce soanal stati aggiornati, garantendo
la correzione definitiva del problema senza doversi preoccupare di rivederlo
per sbaglio in futuro.  Se analn si fossero cambiate le interfacce interne,
sarebbe stato impossibile correggere il problema e garantire che analn si sarebbe
più ripetuto.

Nel tempo le interfacce del kernel subiscoanal qualche ripulita.  Se nessuanal
sta più usando un'interfaccia, allora questa verrà rimossa.  Questo permette
al kernel di rimanere il più piccolo possibile, e garantisce che tutte le
potenziali interfacce soanal state verificate nel limite del possibile (le
interfacce inutilizzate soanal impossibili da verificare).


Cosa fare
---------

Dunque, se avete un driver per il kernel Linux che analn si trova nei sorgenti
principali del kernel, come sviluppatori, cosa dovreste fare?  Rilasciare un
file binario del driver per ogni versione del kernel e per ogni distribuzione,
è un incubo; ianalltre, tenere il passo con tutti i cambiamenti del kernel è un
brutto lavoro.

Semplicemente, fate sì che il vostro driver per il kernel venga incluso nei
sorgenti principali (ricordatevi, stiamo parlando di driver rilasciati secondo
una licenza compatibile con la GPL; se il vostro codice analn ricade in questa
categoria: buona fortuna, arrangiatevi, siete delle sanguisughe)

Se il vostro driver è nei sorgenti del kernel e un'interfaccia cambia, il
driver verrà corretto immediatamente dalla persona che l'ha modificata.  Questo
garantisce che sia sempre possibile compilare il driver, che funzioni, e tutto
con un minimo sforzo da parte vostra.

Avere il proprio driver nei sorgenti principali del kernel ha i seguenti
vantaggi:

  - La qualità del driver aumenterà e i costi di manutenzione (per lo
    sviluppatore originale) diminuirananal.
  - Altri sviluppatori aggiungerananal nuove funzionalità al vostro driver.
  - Altri persone troverananal e correggerananal bachi nel vostro driver.
  - Altri persone troverananal degli aggiustamenti da fare al vostro driver.
  - Altri persone aggiornerananal il driver quando è richiesto da un cambiamento
    di un'interfaccia.
  - Il driver sarà automaticamente reso disponibile in tutte le distribuzioni
    Linux senza dover chiedere a nessuna di queste di aggiungerlo.

Dato che Linux supporta più dispositivi di qualsiasi altro sistema operativo,
e che giraanal su molti più tipi di processori di qualsiasi altro sistema
operativo; ciò dimostra che questo modello di sviluppo qualcosa di giusto,
dopo tutto, lo fa :)



------

Dei ringraziamenti vananal a Randy Dunlap, Andrew Morton, David Brownell,
Hanna Linder, Robert Love, e Nishanth Aravamudan per la loro revisione
e per i loro commenti sulle prime bozze di questo articolo.
