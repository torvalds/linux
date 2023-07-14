.. include:: ../disclaimer-ita.rst

:Original: :ref:`Documentation/process/maintainer-pgp-guide.rst <pgpguide>`
:Translator: Alessia Mantegazza <amantegazza@vaga.pv.it>

.. _it_pgpguide:

=========================================
La guida a PGP per manutentori del kernel
=========================================

:Author: Konstantin Ryabitsev <konstantin@linuxfoundation.org>

Questo documento è destinato agli sviluppatori del kernel Linux, in particolar
modo ai manutentori. Contiene degli approfondimenti riguardo informazioni che
sono state affrontate in maniera più generale nella sezione
"`Protecting Code Integrity`_" pubblicata dalla Linux Foundation.
Per approfondire alcuni argomenti trattati in questo documento è consigliato
leggere il documento sopraindicato

.. _`Protecting Code Integrity`: https://github.com/lfit/itpol/blob/master/protecting-code-integrity.md

Il ruolo di PGP nello sviluppo del kernel Linux
===============================================

PGP aiuta ad assicurare l'integrità del codice prodotto dalla comunità
di sviluppo del kernel e, in secondo luogo, stabilisce canali di comunicazione
affidabili tra sviluppatori attraverso lo scambio di email firmate con PGP.

Il codice sorgente del kernel Linux è disponibile principalmente in due
formati:

- repositori distribuiti di sorgenti (git)
- rilasci periodici di istantanee (archivi tar)

Sia i repositori git che gli archivi tar portano le firme PGP degli
sviluppatori che hanno creato i rilasci ufficiali del kernel. Queste firme
offrono una garanzia crittografica che le versioni scaricabili rese disponibili
via kernel.org, o altri portali, siano identiche a quelle che gli sviluppatori
hanno sul loro posto di lavoro. A tal scopo:

- i repositori git forniscono firme PGP per ogni tag
- gli archivi tar hanno firme separate per ogni archivio

.. _it_devs_not_infra:

Fidatevi degli sviluppatori e non dell'infrastruttura
-----------------------------------------------------

Fin dal 2011, quando i sistemi di kernel.org furono compromessi, il principio
generale del progetto Kernel Archives è stato quello di assumere che qualsiasi
parte dell'infrastruttura possa essere compromessa in ogni momento. Per questa
ragione, gli amministratori hanno intrapreso deliberatemene dei passi per
enfatizzare che la fiducia debba risiedere sempre negli sviluppatori e mai nel
codice che gestisce l'infrastruttura, indipendentemente da quali che siano le
pratiche di sicurezza messe in atto.

Il principio sopra indicato è la ragione per la quale è necessaria questa
guida. Vogliamo essere sicuri che il riporre la fiducia negli sviluppatori
non sia fatto semplicemente per incolpare qualcun'altro per future falle di
sicurezza. L'obiettivo è quello di fornire una serie di linee guida che gli
sviluppatori possano seguire per creare un ambiente di lavoro sicuro e
salvaguardare le chiavi PGP usate nello stabilire l'integrità del kernel Linux
stesso.

.. _it_pgp_tools:

Strumenti PGP
=============

Usare GnuPG 2.2 o successivo
----------------------------

La vostra distribuzione potrebbe avere già installato GnuPG, dovete solo
verificare che stia utilizzando la versione abbastanza recente. Per controllate
usate::

    $ gpg --version | head -n1

Se state utilizzando la version 2.2 o successiva, allora siete pronti a partire.
Se invece state usando una versione precedente, allora alcuni comandi elencati
in questa guida potrebbero non funzionare.

Configurare le opzioni di gpg-agent
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

L'agente GnuPG è uno strumento di aiuto che partirà automaticamente ogni volta
che userete il comando ``gpg`` e funzionerà in *background* con l'obiettivo di
individuare la passphrase. Ci sono due opzioni che dovreste conoscere
per personalizzare la scadenza della passphrase nella cache:

- ``default-cache-ttl`` (secondi): Se usate ancora la stessa chiave prima
  che il time-to-live termini, il conto alla rovescia si resetterà per un
  altro periodo. Di base è di 600 (10 minuti).

- ``max-cache-ttl`` (secondi): indipendentemente da quanto sia recente l'ultimo
  uso della chiave da quando avete inserito la passphrase, se il massimo
  time-to-live è scaduto, dovrete reinserire nuovamente la passphrase.
  Di base è di 30 minuti.

Se ritenete entrambe questi valori di base troppo corti (o troppo lunghi),
potete creare il vostro file ``~/.gnupg/gpg-agent.conf`` ed impostare i vostri
valori::

    # set to 30 minutes for regular ttl, and 2 hours for max ttl
    default-cache-ttl 1800
    max-cache-ttl 7200

.. note::

    Non è più necessario far partire l'agente gpg manualmente all'inizio della
    vostra sessione. Dovreste controllare i file rc per rimuovere tutto ciò che
    riguarda vecchie le versioni di GnuPG, poiché potrebbero non svolgere più
    bene il loro compito.

.. _it_protect_your_key:

Proteggere la vostra chiave PGP primaria
========================================

Questa guida parte dal presupposto che abbiate già una chiave PGP che usate
per lo sviluppo del kernel Linux. Se non ne avete ancora una, date uno sguardo
al documento "`Protecting Code Integrity`_" che abbiamo menzionato prima.

Dovreste inoltre creare una nuova chiave se quella attuale è inferiore a 2048
bit (RSA).

Le sottochiavi PGP
------------------

Raramente le chiavi PGP sono composte da una singola coppia -- solitamente, sono
una collezione di sottochiavi indipendenti usate per diversi scopi in funzione
delle capacità assegnate al momento della creazione. Una chiave PGP può avere
quattro capacità:

- **[S]** può essere usata per firmare
- **[E]** può essere usata per criptare
- **[A]** può essere usata per autenticare
- **[C]** può essere usata per certificare altre chiavi

La chiave con la capacità **[C]** viene spesso chiamata chiave "passepartout"
(*master key*), ma è una terminologia fuorviante perché lascia intendere che la
chiave di certificato possa essere usate in sostituzione delle altre (proprio
come le vere chiavi passpartout in grado di aprire diverse serrature). Dato che
questo non è il caso, per evitare fraintendimenti, in questa guida ci riferiremo
a questa chiave chiamandola "La chiave di certificazione".

I seguenti punti sono molto importanti:

1. Tutte le sottochiavi sono indipendenti. Se perdete una sottochiave privata
   non potrete recuperarla usando le altre.
2. Ad eccezione della chiave di certificazione, ci possono essere più
   sottochiavi con le stesse capacità (per esempio, potete avere 2 sottochiavi
   per criptare, 3 per firmare, ma solo una per una sola per certificare). Tutte
   le sottochiavi sono indipendenti -- un messaggio criptato usando una chiave
   **[E]** non può essere decriptato usano altre sottochiavi **[E]**.
3. Una sottochiave può avere più capacità (per esempio, la chiave **[C]** può
   anche essere una chiave **[S]**).

La chiave con capacità **[C]** (certificazione) è la sola che può essere usata
per indicare relazioni fra chiavi. Solo la chiave **[C]** può essere usata per:

- aggiungere o revocare altre chiavi (sottochiavi) che hanno capacità S/E/A;
- aggiungere, modificare o eliminare le identità (unids) associate alla chiave;
- aggiungere o modificare la propria data di scadenza o delle sottochiavi;
- firmare le chiavi di altre persone a scopo di creare una rete di fiducia.

Di base, alla creazione di nuove chiavi, GnuPG genera quanto segue:

- Una chiave la capacità di certificazione che quella di firma (**[SC]**)
- Una sottochiave separata con capacità di criptare (**[E]**)




Se avete usato i parametri predefiniti per generare la vostra chiave, quello
sarà il risultato. Potete verificarlo utilizzando ``gpg --list-secret-keys``,
per esempio::

    sec   ed25519 2022-12-20 [SC] [expires: 2024-12-19]
          000000000000000000000000AAAABBBBCCCCDDDD
    uid           [ultimate] Alice Dev <adev@kernel.org>
    ssb   cv25519 2022-12-20 [E] [expires: 2024-12-19]

La lunga riga sotto la voce ``sec`` è la vostra impronta digitale --
negli esempi che seguono, quando vedere ``[fpr]`` ci si riferisce a questa
stringa di 40 caratteri.

Assicuratevi che la vostra passphrase sia forte
-----------------------------------------------

GnuPG utilizza le passphrases per criptare la vostra chiave privata prima
di salvarla sul disco. In questo modo, anche se il contenuto della vostra
cartella ``.gnupg`` venisse letto o trafugato nella sia interezza, gli
attaccanti non potrebbero comunque utilizzare le vostre chiavi private senza
aver prima ottenuto la passphrase per decriptarle.

È assolutamente essenziale che le vostre chiavi private siano protette da
una passphrase forte. Per impostarla o cambiarla, usate::

    $ gpg --change-passphrase [fpr]

Create una sottochiave di firma separata
----------------------------------------

Il nostro obiettivo è di proteggere la chiave primaria spostandola su un
dispositivo sconnesso dalla rete, dunque se avete solo una chiave combinata
**[SC]** allora dovreste creare una sottochiave di firma separata::

    $ gpg --quick-add-key [fpr] ed25519 sign

Ricordate di informare il keyserver del vostro cambiamento, cosicché altri
possano ricevere la vostra nuova sottochiave::

    $ gpg --send-key [fpr]

.. note:: Supporto ECC in GnuPG

   Tenete presente che se avete intenzione di usare un dispositivo che non
   supporta chiavi ED25519 ECC, allora dovreste usare "nistp256" al posto di
   "ed25519". Più avanti ci sono alcune raccomandazioni per i dispositivi.

Copia di riserva della chiave primaria per gestire il recupero da disastro
--------------------------------------------------------------------------

Maggiori sono le firme di altri sviluppatori che vengono applicate alla vostra,
maggiori saranno i motivi per avere una copia di riserva che non sia digitale,
al fine di effettuare un recupero da disastro.

Il modo migliore per creare una copia fisica della vostra chiave privata è
l'uso del programma ``paperkey``. Consultate ``man paperkey`` per maggiori
dettagli sul formato dell'output ed i suoi punti di forza rispetto ad altre
soluzioni. Paperkey dovrebbe essere già pacchettizzato per la maggior parte
delle distribuzioni.

Eseguite il seguente comando per creare una copia fisica di riserva della
vostra chiave privata::

    $ gpg --export-secret-key [fpr] | paperkey -o /tmp/key-backup.txt

Stampate il file (o fate un pipe direttamente verso lpr), poi prendete
una penna e scrivete la passphare sul margine del foglio.  **Questo è
caldamente consigliato** perché la copia cartacea è comunque criptata con
la passphrase, e se mai doveste cambiarla non vi ricorderete qual'era al
momento della creazione di quella copia -- *garantito*.

Mettete la copia cartacea e la passphrase scritta a mano in una busta e
mettetela in un posto sicuro e ben protetto, preferibilmente fuori casa,
magari in una cassetta di sicurezza in banca.

.. note::

    Probabilmente la vostra stampante non è più quello stupido dispositivo
    connesso alla porta parallela, ma dato che il suo output è comunque
    criptato con la passphrase, eseguire la stampa in un sistema "cloud"
    moderno dovrebbe essere comunque relativamente sicuro.

Copia di riserva di tutta la cartella GnuPG
-------------------------------------------

.. warning::

    **!!!Non saltate questo passo!!!**

Quando avete bisogno di recuperare le vostre chiavi PGP è importante avere
una copia di riserva pronta all'uso. Questo sta su un diverso piano di
prontezza rispetto al recupero da disastro che abbiamo risolto con
``paperkey``. Vi affiderete a queste copie esterne quando dovreste usare la
vostra chiave Certify -- ovvero quando fate modifiche alle vostre chiavi o
firmate le chiavi di altre persone ad una conferenza o ad un gruppo d'incontro.

Incominciate con una piccola chiavetta di memoria USB (preferibilmente due)
che userete per le copie di riserva. Dovrete criptarle usando LUKS -- fate
riferimento alla documentazione della vostra distribuzione per capire come
fare.

Per la passphrase di criptazione, potete usare la stessa della vostra chiave
primaria.

Una volta che il processo di criptazione è finito, reinserite il disco USB ed
assicurativi che venga montato correttamente. Copiate interamente la cartella
``.gnugp`` nel disco criptato::

    $ cp -a ~/.gnupg /media/disk/foo/gnupg-backup

Ora dovreste verificare che tutto continui a funzionare::

    $ gpg --homedir=/media/disk/foo/gnupg-backup --list-key [fpr]

Se non vedete errori, allora dovreste avere fatto tutto con successo.
Smontate il disco USB, etichettatelo per bene di modo da evitare di
distruggerne il contenuto non appena vi serve una chiavetta USB a caso, ed
infine mettetelo in un posto sicuro -- ma non troppo lontano, perché vi servirà
di tanto in tanto per modificare le identità, aggiungere o revocare
sottochiavi, o firmare le chiavi di altre persone.

Togliete la chiave primaria dalla vostra home
---------------------------------------------

I file che si trovano nella vostra cartella home non sono poi così ben protetti
come potreste pensare. Potrebbero essere letti o trafugati in diversi modi:

- accidentalmente quando fate una rapida copia della cartella home per
  configurare una nuova postazione
- da un amministratore di sistema negligente o malintenzionato
- attraverso copie di riserva insicure
- attraverso malware installato in alcune applicazioni (browser, lettori PDF,
  eccetera)
- attraverso coercizione quando attraversate confini internazionali

Proteggere la vostra chiave con una buona passphare aiuta notevolmente a
ridurre i rischi elencati qui sopra, ma le passphrase possono essere scoperte
attraverso i keylogger, il shoulder-surfing, o altri modi. Per questi motivi,
nella configurazione si raccomanda di rimuove la chiave primaria dalla vostra
cartella home e la si archivia su un dispositivo disconnesso.

.. warning::

    Per favore, fate riferimento alla sezione precedente e assicuratevi
    di aver fatto una copia di riserva totale della cartella GnuPG. Quello
    che stiamo per fare renderà la vostra chiave inutile se non avete delle
    copie di riserva utilizzabili!

Per prima cosa, identificate il keygrip della vostra chiave primaria::

    $ gpg --with-keygrip --list-key [fpr]

L'output assomiglierà a questo::

    pub   ed25519 2022-12-20 [SC] [expires: 2022-12-19]
          000000000000000000000000AAAABBBBCCCCDDDD
          Keygrip = 1111000000000000000000000000000000000000
    uid           [ultimate] Alice Dev <adev@kernel.org>
    sub   cv25519 2022-12-20 [E] [expires: 2022-12-19]
          Keygrip = 2222000000000000000000000000000000000000
    sub   ed25519 2022-12-20 [S]
          Keygrip = 3333000000000000000000000000000000000000

Trovate la voce keygrid che si trova sotto alla riga ``pub`` (appena sotto
all'impronta digitale della chiave primaria). Questo corrisponderà direttamente
ad un file nella cartella ``~/.gnupg``::

    $ cd ~/.gnupg/private-keys-v1.d
    $ ls
    1111000000000000000000000000000000000000.key
    2222000000000000000000000000000000000000.key
    3333000000000000000000000000000000000000.key

Quello che dovrete fare è rimuovere il file .key che corrisponde al keygrip
della chiave primaria::

    $ cd ~/.gnupg/private-keys-v1.d
    $ rm 1111000000000000000000000000000000000000.key

Ora, se eseguite il comando ``--list-secret-keys``, vedrete che la chiave
primaria non compare più (il simbolo ``#`` indica che non è disponibile)::

    $ gpg --list-secret-keys
    sec#  ed25519 2022-12-20 [SC] [expires: 2024-12-19]
          000000000000000000000000AAAABBBBCCCCDDDD
    uid           [ultimate] Alice Dev <adev@kernel.org>
    ssb   cv25519 2022-12-20 [E] [expires: 2024-12-19]
    ssb   ed25519 2022-12-20 [S]

Dovreste rimuovere anche i file ``secring.gpg`` che si trovano nella cartella
``~/.gnupg``, in quanto rimasugli delle versioni precedenti di GnuPG.

Se non avete la cartella "private-keys-v1.d"
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Se non avete la cartella ``~/.gnupg/private-keys-v1.d``, allora le vostre
chiavi segrete sono ancora salvate nel vecchio file ``secring.gpg`` usato
da GnuPG v1. Effettuare una qualsiasi modifica alla vostra chiave, come
cambiare la passphare o aggiungere una sottochiave, dovrebbe convertire
automaticamente il vecchio formato ``secring.gpg``nel nuovo
``private-keys-v1.d``.

Una volta che l'avete fatto, assicuratevi di rimuovere il file ``secring.gpg``,
che continua a contenere la vostra chiave privata.

.. _it_smartcards:

Spostare le sottochiavi in un apposito dispositivo criptato
===========================================================

Nonostante la chiave primaria sia ora al riparo da occhi e mani indiscrete,
le sottochiavi si trovano ancora nella vostra cartella home. Chiunque riesca
a mettere le sue mani su quelle chiavi riuscirà a decriptare le vostre
comunicazioni o a falsificare le vostre firme (se conoscono la passphrase).
Inoltre, ogni volta che viene fatta un'operazione con GnuPG, le chiavi vengono
caricate nella memoria di sistema e potrebbero essere rubate con l'uso di
malware sofisticati (pensate a Meltdown e a Spectre).

Il miglior modo per proteggere le proprie chiave è di spostarle su un
dispositivo specializzato in grado di effettuare operazioni smartcard.

I benefici di una smartcard
---------------------------

Una smartcard contiene un chip crittografico che è capace di immagazzinare
le chiavi private ed effettuare operazioni crittografiche direttamente sulla
carta stessa. Dato che la chiave non lascia mai la smartcard, il sistema
operativo usato sul computer non sarà in grado di accedere alle chiavi.
Questo è molto diverso dai dischi USB criptati che abbiamo usato allo scopo di
avere una copia di riserva sicura -- quando il dispositivo USB è connesso e
montato, il sistema operativo potrà accedere al contenuto delle chiavi private.

L'uso di un disco USB criptato non può sostituire le funzioni di un dispositivo
capace di operazioni di tipo smartcard.

Dispositivi smartcard disponibili
---------------------------------

A meno che tutti i vostri computer dispongano di lettori smartcard, il modo
più semplice è equipaggiarsi di un dispositivo USB specializzato che
implementi le funzionalità delle smartcard.  Sul mercato ci sono diverse
soluzioni disponibili:

- `Nitrokey Start`_: è Open hardware e Free Software, è basata sul progetto
  `GnuK`_ della FSIJ. Questo è uno dei pochi dispositivi a supportare le chiavi
  ECC ED25519, ma offre meno funzionalità di sicurezza (come la resistenza
  alla manomissione o alcuni attacchi ad un canale laterale).
- `Nitrokey Pro 2`_: è simile alla Nitrokey Start, ma è più resistente alla
  manomissione e offre più funzionalità di sicurezza. La Pro 2 supporta la
  crittografia ECC (NISTP).
- `Yubikey 5`_: l'hardware e il software sono proprietari, ma è più economica
  della  Nitrokey Pro ed è venduta anche con porta USB-C il che è utile con i
  computer portatili più recenti. In aggiunta, offre altre funzionalità di
  sicurezza come FIDO, U2F, e ora supporta anche le chiavi ECC (NISTP)

La vostra scelta dipenderà dal costo, la disponibilità nella vostra regione, e
sulla scelta fra dispositivi aperti e proprietari.

.. note::

    Se siete nella lista MAINTAINERS o avete un profilo su kernel.org, allora
    `potrete avere gratuitamente una Nitrokey Start`_ grazie alla fondazione
    Linux.

.. _`Nitrokey Start`: https://shop.nitrokey.com/shop/product/nitrokey-start-6
.. _`Nitrokey Pro 2`: https://shop.nitrokey.com/shop/product/nitrokey-pro-2-3
.. _`Yubikey 5`: https://www.yubico.com/product/yubikey-5-overview/
.. _Gnuk: https://www.fsij.org/doc-gnuk/
.. _`potrete avere gratuitamente una Nitrokey Start`: https://www.kernel.org/nitrokey-digital-tokens-for-kernel-developers.html

Configurare il vostro dispositivo smartcard
-------------------------------------------

Il vostro dispositivo smartcard dovrebbe iniziare a funzionare non appena
lo collegate ad un qualsiasi computer Linux moderno. Potete verificarlo
eseguendo::

    $ gpg --card-status

Se vedete tutti i dettagli della smartcard, allora ci siamo. Sfortunatamente,
affrontare tutti i possibili motivi per cui le cose potrebbero non funzionare
non è lo scopo di questa guida. Se avete problemi nel far funzionare la carta
con GnuPG, cercate aiuto attraverso i soliti canali di supporto.

Per configurare la vostra smartcard, dato che non c'è una via facile dalla
riga di comando, dovrete usate il menu di GnuPG::

    $ gpg --card-edit
    [...omitted...]
    gpg/card> admin
    Admin commands are allowed
    gpg/card> passwd

Dovreste impostare il PIN dell'utente (1), quello dell'amministratore (3) e il
codice di reset (4). Assicuratevi di annotare e salvare questi codici in un
posto sicuro -- specialmente il PIN dell'amministratore e il codice di reset
(che vi permetterà di azzerare completamente la smartcard).  Il PIN
dell'amministratore viene usato così raramente che è inevitabile dimenticarselo
se non lo si annota.

Tornando al nostro menu, potete impostare anche altri valori (come il nome,
il sesso, informazioni d'accesso, eccetera), ma non sono necessari e aggiunge
altre informazioni sulla carta che potrebbero trapelare in caso di smarrimento.

.. note::

    A dispetto del nome "PIN", né il PIN utente né quello dell'amministratore
    devono essere esclusivamente numerici.

.. warning::

    Alcuni dispositivi richiedono la presenza delle sottochiavi nel dispositivo
    stesso prima che possiate cambiare la passphare. Verificate la
    documentazione del produttore.

Spostare le sottochiavi sulla smartcard
---------------------------------------

Uscite dal menu (usando "q") e salverete tutte le modifiche. Poi, spostiamo
tutte le sottochiavi sulla smartcard. Per la maggior parte delle operazioni
vi serviranno sia la passphrase della chiave PGP che il PIN
dell'amministratore::

    $ gpg --edit-key [fpr]

    Secret subkeys are available.

    pub  ed25519/AAAABBBBCCCCDDDD
         created: 2022-12-20  expires: 2024-12-19  usage: SC
         trust: ultimate      validity: ultimate
    ssb  cv25519/1111222233334444
         created: 2022-12-20  expires: never       usage: E
    ssb  ed25519/5555666677778888
         created: 2017-12-07  expires: never       usage: S
    [ultimate] (1). Alice Dev <adev@kernel.org>

    gpg>

Usando ``--edit-key`` si tornerà alla modalità menu e noterete che
la lista delle chiavi è leggermente diversa. Da questo momento in poi,
tutti i comandi saranno eseguiti nella modalità menu, come indicato
da ``gpg>``.

Per prima cosa, selezioniamo la chiave che verrà messa sulla carta --
potete farlo digitando ``key 1`` (è la prima della lista, la sottochiave
**[E]**)::

    gpg> key 1

Nel'output dovreste vedere ``ssb*`` associato alla chiave **[E]**. Il simbolo
``*`` indica che la chiave è stata "selezionata". Funziona come un
interruttore, ovvero se scrivete nuovamente ``key 1``, il simbolo ``*`` sparirà
e la chiave non sarà più selezionata.

Ora, spostiamo la chiave sulla smartcard::

    gpg> keytocard
    Please select where to store the key:
       (2) Encryption key
    Your selection? 2

Dato che è la nostra chiave  **[E]**, ha senso metterla nella sezione criptata.
Quando confermerete la selezione, vi verrà chiesta la passphrase della vostra
chiave PGP, e poi il PIN dell'amministratore. Se il comando ritorna senza
errori, allora la vostra chiave è stata spostata con successo.

**Importante**: digitate nuovamente ``key 1`` per deselezionare la prima chiave
e selezionate la seconda chiave **[S]** con ``key 2``::

    gpg> key 1
    gpg> key 2
    gpg> keytocard
    Please select where to store the key:
       (1) Signature key
       (3) Authentication key
    Your selection? 1

Potete usare la chiave **[S]** sia per firmare che per autenticare, ma vogliamo
che sia nella sezione di firma, quindi scegliete (1). Ancora una volta, se il
comando ritorna senza errori, allora l'operazione è avvenuta con successo::

    gpg> q
    Save changes? (y/N) y

Salvando le modifiche cancellerete dalla vostra cartella home tutte le chiavi
che avete spostato sulla carta (ma questo non è un problema, perché abbiamo
fatto delle copie di sicurezza nel caso in cui dovessimo configurare una
nuova smartcard).

Verificare che le chiavi siano state spostate
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Ora, se doveste usare l'opzione ``--list-secret-keys``, vedrete una
sottile differenza nell'output::

    $ gpg --list-secret-keys
    sec#  ed25519 2022-12-20 [SC] [expires: 2024-12-19]
          000000000000000000000000AAAABBBBCCCCDDDD
    uid           [ultimate] Alice Dev <adev@kernel.org>
    ssb>  cv25519 2022-12-20 [E] [expires: 2024-12-19]
    ssb>  ed25519 2022-12-20 [S]

Il simbolo ``>`` in ``ssb>`` indica che la sottochiave è disponibile solo
nella smartcard. Se tornate nella vostra cartella delle chiavi segrete e
guardate al suo contenuto, noterete che i file ``.key`` sono stati sostituiti
con degli stub::

    $ cd ~/.gnupg/private-keys-v1.d
    $ strings *.key | grep 'private-key'

Per indicare che i file sono solo degli stub e che in realtà il contenuto è
sulla smartcard, l'output dovrebbe mostrarvi ``shadowed-private-key``.

Verificare che la smartcard funzioni
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Per verificare che la smartcard funzioni come dovuto, potete creare
una firma::

    $ echo "Hello world" | gpg --clearsign > /tmp/test.asc
    $ gpg --verify /tmp/test.asc

Col primo comando dovrebbe chiedervi il PIN della smartcard, e poi dovrebbe
mostrare "Good signature" dopo l'esecuzione di ``gpg --verify``.

Complimenti, siete riusciti a rendere estremamente difficile il furto della
vostra identità digitale di sviluppatore.

Altre operazioni possibili con GnuPG
------------------------------------

Segue un breve accenno ad alcune delle operazioni più comuni che dovrete
fare con le vostre chiavi PGP.

Montare il disco con la chiave primaria
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Vi servirà la vostra chiave principale per tutte le operazioni che seguiranno,
per cui per prima cosa dovrete accedere ai vostri backup e dire a GnuPG di
usarli::

    $ export GNUPGHOME=/media/disk/foo/gnupg-backup
    $ gpg --list-secret-keys

Dovete assicurarvi di vedere ``sec`` e non ``sec#`` nell'output del programma
(il simbolo ``#`` significa che la chiave non è disponibile e che state ancora
utilizzando la vostra solita cartella di lavoro).

Estendere la data di scadenza di una chiave
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

La chiave principale ha una data di scadenza di 2 anni dal momento della sua
creazione. Questo per motivi di sicurezza e per rendere obsolete le chiavi
che, eventualmente, dovessero sparire dai keyserver.

Per estendere di un anno, dalla data odierna, la scadenza di una vostra chiave,
eseguite::

    $ gpg --quick-set-expire [fpr] 1y

Se per voi è più facile da memorizzare, potete anche utilizzare una data
specifica (per esempio, il vostro compleanno o capodanno)::

    $ gpg --quick-set-expire [fpr] 2025-07-01

Ricordatevi di inviare l'aggiornamento ai keyserver::

    $ gpg --send-key [fpr]

Aggiornare la vostra cartella di lavoro dopo ogni modifica
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Dopo aver fatto delle modifiche alle vostre chiavi usando uno spazio a parte,
dovreste importarle nella vostra cartella di lavoro abituale::

    $ gpg --export | gpg --homedir ~/.gnupg --import
    $ unset GNUPGHOME

Usare gpg-agent con ssh
~~~~~~~~~~~~~~~~~~~~~~~

Se dovete firmare tag o commit su un sistema remoto, potete ridirezionare il
vostro gpg-agent attraverso ssh. Consultate le istruzioni disponibili nella wiki
GnuPG:

- `Agent Forwarding over SSH`_

Funziona senza troppi intoppi se avete la possibilità di modificare le
impostazioni di sshd sul sistema remoto.

.. _`Agent Forwarding over SSH`: https://wiki.gnupg.org/AgentForwarding

.. _it_pgp_with_git:

Usare PGP con Git
=================

Una delle caratteristiche fondanti di Git è la sua natura decentralizzata --
una volta che il repositorio è stato clonato sul vostro sistema, avete la
storia completa del progetto, inclusi i suoi tag, i commit ed i rami. Tuttavia,
con i centinaia di repositori clonati che ci sono in giro, come si fa a
verificare che la loro copia di linux.git non è stata manomessa da qualcuno?

Oppure, cosa succede se viene scoperta una backdoor nel codice e la riga
"Autore" dice che sei stato tu, mentre tu sei abbastanza sicuro di
`non averci niente a che fare`_?

Per risolvere entrambi i problemi, Git ha introdotto l'integrazione con PGP.
I tag firmati dimostrano che il repositorio è integro assicurando che il suo
contenuto è lo stesso che si trova sulle macchine degli sviluppatori che hanno
creato il tag; mentre i commit firmati rendono praticamente impossibile
ad un malintenzionato di impersonarvi senza avere accesso alle vostre chiavi
PGP.

.. _`non averci niente a che fare`: https://github.com/jayphelps/git-blame-someone-else

Configurare git per usare la vostra chiave PGP
----------------------------------------------

Se avete solo una chiave segreta nel vostro portachiavi, allora non avete nulla
da fare in più dato che sarà la vostra chiave di base. Tuttavia, se doveste
avere più chiavi segrete, potete dire a git quale dovrebbe usare (``[fpg]``
è la vostra impronta digitale)::

    $ git config --global user.signingKey [fpr]

Come firmare i tag
------------------

Per creare un tag firmato, passate l'opzione ``-s`` al comando tag::

    $ git tag -s [tagname]

La nostra raccomandazione è quella di firmare sempre i tag git, perché
questo permette agli altri sviluppatori di verificare che il repositorio
git dal quale stanno prendendo il codice non è stato alterato intenzionalmente.

Come verificare i tag firmati
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Per verificare un tag firmato, potete usare il comando ``verify-tag``::

    $ git verify-tag [tagname]

Se state prendendo un tag da un fork del repositorio del progetto, git
dovrebbe verificare automaticamente la firma di quello che state prendendo
e vi mostrerà il risultato durante l'operazione di merge::

    $ git pull [url] tags/sometag

Il merge conterrà qualcosa di simile::

    Merge tag 'sometag' of [url]

    [Tag message]

    # gpg: Signature made [...]
    # gpg: Good signature from [...]

Se state verificando il tag di qualcun altro, allora dovrete importare
la loro chiave PGP. Fate riferimento alla sezione ":ref:`it_verify_identities`"
che troverete più avanti.

Configurare git per firmare sempre i tag con annotazione
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Se state creando un tag con annotazione è molto probabile che vogliate
firmarlo. Per imporre a git di firmare sempre un tag con annotazione,
dovete impostare la seguente opzione globale::

    $ git config --global tag.forceSignAnnotated true

Come usare commit firmati
-------------------------

Creare dei commit firmati è facile, ma è molto più difficile utilizzarli
nello sviluppo del kernel linux per via del fatto che ci si affida alle
liste di discussione e questo modo di procedere non mantiene le firme PGP
nei commit. In aggiunta, quando si usa *rebase* nel proprio repositorio
locale per allinearsi al kernel anche le proprie firme PGP verranno scartate.
Per questo motivo, la maggior parte degli sviluppatori del kernel non si
preoccupano troppo di firmare i propri commit ed ignoreranno quelli firmati
che si trovano in altri repositori usati per il proprio lavoro.

Tuttavia, se avete il vostro repositorio di lavoro disponibile al pubblico
su un qualche servizio di hosting git (kernel.org, infradead.org, ozlabs.org,
o altri), allora la raccomandazione è di firmare tutti i vostri commit
anche se gli sviluppatori non ne beneficeranno direttamente.

Vi raccomandiamo di farlo per i seguenti motivi:

1. Se dovesse mai esserci la necessità di fare delle analisi forensi o
   tracciare la provenienza di un codice, anche sorgenti mantenuti
   esternamente che hanno firme PGP sui commit avranno un certo valore a
   questo scopo.
2. Se dovesse mai capitarvi di clonare il vostro repositorio locale (per
   esempio dopo un danneggiamento del disco), la firma vi permetterà di
   verificare l'integrità del repositorio prima di riprendere il lavoro.
3. Se qualcuno volesse usare *cherry-pick* sui vostri commit, allora la firma
   permetterà di verificare l'integrità dei commit prima di applicarli.

Creare commit firmati
~~~~~~~~~~~~~~~~~~~~~

Per creare un commit firmato, dovete solamente aggiungere l'opzione ``-S``
al comando ``git commit`` (si usa la lettera maiuscola per evitare
conflitti con un'altra opzione)::

    $ git commit -S

Configurare git per firmare sempre i commit
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Potete dire a git di firmare sempre i commit::

    git config --global commit.gpgSign true

.. note::

    Assicuratevi di aver configurato ``gpg-agent`` prima di abilitare
    questa opzione.

.. _it_verify_identities:

Come lavorare con patch firmate
-------------------------------

Esiste la possibilità di usare la vostra chiave PGP per firmare le patch che
invierete alla liste di discussione del kernel. I meccanismi esistenti per la
firma delle email (PGP-Mime o PGP-inline) tendono a causare problemi
nell'attività di revisione del codice. Si suggerisce, invece, di utilizare lo
strumento sviluppato da kernel.org che mette nell'intestazione del messaggio
un'attestazione delle firme crittografiche (tipo DKIM):

- `Patatt Patch Attestation`_

.. _`Patatt Patch Attestation`: https://pypi.org/project/patatt/

Installare e configurate patatt
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Lo strumento patatt è disponibile per diverse distribuzioni, dunque cercatelo
prima lì. Oppure potete installarlo usano pypi "``pip install patatt``"

Se avete già configurato git con la vostra chiave PGP (usando
``user.signingKey``), allora patatt non ha bisogno di alcuna configurazione
aggiuntiva. Potete iniziare a firmare le vostre patch aggiungendo un aggancio a
git-send-email nel vostro repositorio::

    patatt install-hook

Ora, qualsiasi patch che invierete con ``git send-email`` verrà automaticamente
firmata usando la vostra firma crittografica.

Verificare le firme di patatt
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Se usate ``b4`` per verificare ed applicare le patch, allora tenterà
automaticamente di verificare tutte le firme DKIM e patatt disponibili. Per
esempio::

    $ b4 am 20220720205013.890942-1-broonie@kernel.org
    [...]
    Checking attestation on all messages, may take a moment...
    ---
      ✓ [PATCH v1 1/3] kselftest/arm64: Correct buffer allocation for SVE Z registers
      ✓ [PATCH v1 2/3] arm64/sve: Document our actual ABI for clearing registers on syscall
      ✓ [PATCH v1 3/3] kselftest/arm64: Enforce actual ABI for SVE syscalls
      ---
      ✓ Signed: openpgp/broonie@kernel.org
      ✓ Signed: DKIM/kernel.org

.. note::

   Lo sviluppo di patatt e b4 è piuttosto attivo. Si consiglia di verificare la
   documentazione più recente.

.. _it_kernel_identities:

Come verificare l'identità degli sviluppatori del kernel
========================================================

Firmare i tag e i commit è facile, ma come si fa a verificare che la chiave
usata per firmare qualcosa appartenga davvero allo sviluppatore e non ad un
impostore?

Configurare l'auto-key-retrieval usando WKD e DANE
--------------------------------------------------

Se non siete ancora in possesso di una vasta collezione di chiavi pubbliche
di altri sviluppatori, allora potreste iniziare il vostro portachiavi
affidandovi ai servizi di auto-scoperta e auto-recupero. GnuPG può affidarsi
ad altre tecnologie di delega della fiducia, come DNSSEC e TLS, per sostenervi
nel caso in cui iniziare una propria rete di fiducia da zero sia troppo
scoraggiante.

Aggiungete il seguente testo al vostro file ``~/.gnupg/gpg.conf``::

    auto-key-locate wkd,dane,local
    auto-key-retrieve

La *DNS-Based Authentication of Named Entities* ("DANE") è un metodo
per la pubblicazione di chiavi pubbliche su DNS e per renderle sicure usando
zone firmate con DNSSEC. Il *Web Key Directory* ("WKD") è un metodo
alternativo che usa https a scopo di ricerca. Quando si usano DANE o WKD
per la ricerca di chiavi pubbliche, GnuPG validerà i certificati DNSSEC o TLS
prima di aggiungere al vostro portachiavi locale le eventuali chiavi trovate.

Kernel.org pubblica la WKD per tutti gli sviluppatori che hanno un account
kernel.org. Una volta che avete applicato le modifiche al file ``gpg.conf``,
potrete auto-recuperare le chiavi di Linus Torvalds e Greg Kroah-Hartman
(se non le avete già)::

    $ gpg --locate-keys torvalds@kernel.org gregkh@kernel.org

Se avete un account kernel.org, al fine di rendere più utile l'uso di WKD
da parte di altri sviluppatori del kernel, dovreste `aggiungere alla vostra
chiave lo UID di kernel.org`_.

.. _`aggiungere alla vostra chiave lo UID di kernel.org`: https://korg.wiki.kernel.org/userdoc/mail#adding_a_kernelorg_uid_to_your_pgp_key

Web of Trust (WOT) o Trust on First Use (TOFU)
----------------------------------------------

PGP incorpora un meccanismo di delega della fiducia conosciuto come
"Web of Trust". Di base, questo è un tentativo di sostituire la necessità
di un'autorità certificativa centralizzata tipica del mondo HTTPS/TLS.
Invece di avere svariati produttori software che decidono chi dovrebbero
essere le entità di certificazione di cui dovreste fidarvi, PGP lascia
la responsabilità ad ogni singolo utente.

Sfortunatamente, solo poche persone capiscono come funziona la rete di fiducia.
Nonostante sia un importante aspetto della specifica OpenPGP, recentemente
le versioni di GnuPG (2.2 e successive) hanno implementato un meccanisco
alternativo chiamato "Trust on First Use" (TOFU). Potete pensare a TOFU come
"ad un approccio all fidicia simile ad SSH". In SSH, la prima volta che vi
connettete ad un sistema remoto, l'impronta digitale della chiave viene
registrata e ricordata. Se la chiave dovesse cambiare in futuro, il programma
SSH vi avviserà e si rifiuterà di connettersi, obbligandovi a prendere una
decisione circa la fiducia che riponete nella nuova chiave. In modo simile,
la prima volta che importate la chiave PGP di qualcuno, si assume sia valida.
Se ad un certo punto GnuPG trova un'altra chiave con la stessa identità,
entrambe, la vecchia e la nuova, verranno segnate come invalide e dovrete
verificare manualmente quale tenere.

Vi raccomandiamo di usare il meccanisco TOFU+PGP (che è la nuova configurazione
di base di GnuPG v2). Per farlo, aggiungete (o modificate) l'impostazione
``trust-model`` in ``~/.gnupg/gpg.conf``::

    trust-model tofu+pgp

Usare il repositorio kernel.org per il web of trust
---------------------------------------------------

Il progetto kernel.org mantiene un repositorio git con le chiavi pubbliche degli sviluppatori in alternativa alla replica dei server di chiavi che negli ultimi anni sono spariti. La documentazione completa su come impostare il repositorio come vostra sorgente di chiavi pubbliche può essere trovato qui:

- `Kernel developer PGP Keyring`_

Se siete uno sviluppatore del kernel, per favore valutate l'idea di inviare la
vostra chiave per l'inclusione in quel portachiavi.


If you are a kernel developer, please consider submitting your key for
inclusion into that keyring.

.. _`Kernel developer PGP Keyring`: https://korg.docs.kernel.org/pgpkeys.html
