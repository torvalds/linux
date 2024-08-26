.. include:: ../disclaimer-ita.rst

:Original: :ref:`Documentation/process/stable-kernel-rules.rst <stable_kernel_rules>`
:Translator: Federico Vaga <federico.vaga@vaga.pv.it>

.. _it_stable_kernel_rules:

Tutto quello che volevate sapere sui rilasci -stable di Linux
==============================================================

Regole sul tipo di patch che vengono o non vengono accettate nei sorgenti
"-stable":

- Questa patch o una equivalente deve esistere già nei sorgenti principali di
  Linux (upstream)
- Ovviamente dev'essere corretta e verificata.
- Non dev'essere più grande di 100 righe, incluso il contesto.
- Deve rispettare le regole scritte in
  :ref:`Documentation/translations/it_IT/process/submitting-patches.rst <it_submittingpatches>`
- Deve correggere un vero baco che causi problemi agli utenti oppure aggiunge
  un nuovo identificatore di dispositivo. Maggiori dettagli per il primo caso:

  - Corregge un problema come un oops, un blocco, una corruzione di dati, un
    vero problema di sicurezza, una stranezza hardware, un problema di
    compilazione (ma non per cose già segnate con CONFIG_BROKEN), o problemi
    del tipo "oh, questo non va bene".
  - Problemi importanti riportati dagli utenti di una distribuzione potrebbero
    essere considerati se correggono importanti problemi di prestazioni o di
    interattività. Dato che questi problemi non sono così ovvi e la loro
    correzione ha un'alta probabilità d'introdurre una regressione,
    dovrebbero essere sottomessi solo dal manutentore della distribuzione
    includendo un link, se esiste, ad un rapporto su bugzilla, e informazioni
    aggiuntive sull'impatto che ha sugli utenti.
  - Non si accettano cose del tipo "Questo potrebbe essere un problema ..."
    come una teorica sezione critica, senza aver fornito anche una spiegazione
    su come il baco possa essere sfruttato.
  - Non deve includere alcuna correzione "banale" (correzioni grammaticali,
    pulizia dagli spazi bianchi, eccetera).

Procedura per sottomettere patch per i sorgenti -stable
-------------------------------------------------------

.. note::
  Una patch di sicurezza non dovrebbe essere gestita (solamente) dal processo
  di revisione -stable, ma dovrebbe seguire le procedure descritte in
  :ref:`Documentation/translations/it_IT/process/security-bugs.rst <it_securitybugs>`.

Ci sono tre opzioni per inviare una modifica per i sorgenti -stable:

1. Aggiungi un'etichetta 'stable' alla descrizione della patch al momento della
   sottomissione per l'inclusione nei sorgenti principali.
2. Chiedere alla squadra "stable" di prendere una patch già applicata sui
   sorgenti principali
3. Sottomettere una patch alla squadra "stable" equivalente ad una modifica già
   fatta sui sorgenti principali.

Le seguenti sezioni descrivono con maggiori dettagli ognuna di queste opzioni

L':ref:`it_option_1` è **fortemente** raccomandata; è il modo più facile e
usato. L':ref:`it_option_2` si usa quando al momento della sottomissione non si
era pensato di riportare la modifica su versioni precedenti.
L':ref:`it_option_3` è un'alternativa ai due metodi precedenti quando la patch
nei sorgenti principali ha bisogno di aggiustamenti per essere applicata su
versioni precedenti (per esempio a causa di cambiamenti dell'API).

Quando si utilizza l'opzione 2 o 3 è possibile chiedere che la modifica sia
inclusa in specifiche versioni stabili. In tal caso, assicurarsi che la correzione
o una equivalente sia applicabile, o già presente in tutti i sorgenti
stabili più recenti ancora supportati. Questo ha lo scopo di prevenire
regressioni che gli utenti potrebbero incontrare in seguito durante
l'aggiornamento, se ad esempio una correzione per 5.19-rc1 venisse
riportata a 5.10.y, ma non a 5.15.y.

.. _it_option_1:

Opzione 1
*********

Aggiungete la seguente etichetta nell'area delle firme per far sì che una patch
che state inviando per l'inclusione nei sorgenti principali venga presa
automaticamente anche per quelli stabili::

  Cc: stable@vger.kernel.org

Invece, usate ``Cc: stable@vger.kernel.org`` quando state inviando correzioni
per vulnerabilità non ancora di pubblico dominio: questo riduce il rischio di
esporre accidentalmente al pubblico la correzione quando si usa 'git
send-email', perché i messaggi inviati a quell'indirizzo non vengono inviati da
nessuna parte.

Una volta che la patch è stata inclusa, verrà applicata anche sui sorgenti
stabili senza che l'autore o il manutentore del sottosistema debba fare
qualcosa.

Per lasciare una nota per la squadra "stable", usate commenti in linea in stile
shell (leggere oltre per maggiori dettagli).

* Specificate i prerequisiti per le patch aggiuntive::

    Cc: <stable@vger.kernel.org> # 3.3.x: a1f84a3: sched: Check for idle
    Cc: <stable@vger.kernel.org> # 3.3.x: 1b9508f: sched: Rate-limit newidle
    Cc: <stable@vger.kernel.org> # 3.3.x: fd21073: sched: Fix affinity logic
    Cc: <stable@vger.kernel.org> # 3.3.x
    Signed-off-by: Ingo Molnar <mingo@elte.hu>

  La sequenza di etichette ha il seguente significato::

     git cherry-pick a1f84a3
     git cherry-pick 1b9508f
     git cherry-pick fd21073
     git cherry-pick <this commit>

  Notate che per una serie di patch non dovere elencare come necessarie tutte
  le patch della serie stessa. Per esempio se avete la seguente serie::

     patch1
     patch2

  dove patch2 dipende da patch1, non dovete elencare patch1 come requisito per
  patch2 se avete già menzionato patch1 per l'inclusione in "stable"

* Evidenziate le patch che hanno dei requisiti circa la versione del kernel::

    Cc: <stable@vger.kernel.org> # 3.3.x

  L'etichetta ha il seguente significato::

     git cherry-pick <this commit>

  per ogni sorgente "-stable" che inizia con la versione indicata.

  Notate che queste etichette non sono necessarie se la squadre "stable" può
  dedurre la versione dalle etichette Fixes:

* Ritardare l'inclusione di patch::
    Cc: <stable@vger.kernel.org> # after -rc3

* Evidenziare problemi noti::

     Cc: <stable@vger.kernel.org> # see patch description, needs adjustments for <= 6.3

Esiste un'ulteriore variante per l'etichetta "stable" che permette di comunicare
allo strumento di *backporting* di ignorare un cambiamento::

     Cc: <stable+noautosel@kernel.org> # reason goes here, and must be present


.. _it_option_2:

Opzione 2
*********

Se la patch è già stata inclusa nei sorgenti Linux, inviate una mail a
stable@vger.kernel.org includendo: il titolo della patch, l'identificativo
del commit, il perché pensate che debba essere applicata, e in quali versioni
del kernel la vorreste vedere.

.. _it_option_3:

Opzione 3
*********

Dopo aver verificato che rispetta le regole descritte in precedenza, inviata la
patch a stable@vger.kernel.org facendo anche menzione delle versioni nella quale
si vorrebbe applicarla. Nel farlo, dovete annotare nel changelog
l'identificativo del commit nei sorgenti principali, così come la versione del
kernel nel quale vorreste vedere la patch.::

    commit <sha1> upstream.

o in alternativa::

    [ Upstream commit <sha1>  ]

Se la patch inviata devia rispetto all'originale presente nei sorgenti
principali (per esempio per adattarsi ad un cambiamento di API), allora questo
dev'essere giustificato e dettagliato in modo chiaro nella descrizione.

Dopo la sottomissione
---------------------

Il mittente riceverà un ACK quando la patch è stata accettata e messa in coda,
oppure un NAK se la patch è stata rigettata. La risposta potrebbe richiedere
alcuni giorni in funzione dei piani dei membri della squadra "stable",

Se accettata, la patch verrà aggiunta alla coda -stable per essere revisionata
dal altri sviluppatori e dal principale manutentore del sottosistema.

Ciclo di una revisione
----------------------

- Quando i manutentori -stable decidono di fare un ciclo di revisione, le
  patch vengono mandate al comitato per la revisione, ai manutentori soggetti
  alle modifiche delle patch (a meno che il mittente non sia anche il
  manutentore di quell'area del kernel) e in CC: alla lista di discussione
  linux-kernel.
- La commissione per la revisione ha 48 ore per dare il proprio ACK o NACK
  alle patch.
- Se una patch viene rigettata da un membro della commissione, o un membro
  della lista linux-kernel obietta la bontà della patch, sollevando problemi
  che i manutentori ed i membri non avevano compreso, allora la patch verrà
  rimossa dalla coda.
- Le patch che hanno ricevuto un ACK verranno inviate nuovamente come parte di
  un rilascio candidato (-rc) al fine di essere verificate dagli sviluppatori e
  dai testatori.
- Solitamente si pubblica solo una -rc, tuttavia se si riscontrano problemi
  importanti, alcune patch potrebbero essere modificate o essere scartate,
  oppure nuove patch potrebbero essere messe in coda. Dunque, verranno pubblicate
  nuove -rc e così via finché non si ritiene che non vi siano più problemi.
- Si può rispondere ad una -rc scrivendo sulla lista di discussione un'email
  con l'etichetta "Tested-by:". Questa etichetta verrà raccolta ed aggiunta al
  commit rilascio.
- Alla fine del ciclo di revisione il nuovo rilascio -stable conterrà tutte le
  patch che erano in coda e sono state verificate.
- Le patch di sicurezza verranno accettate nei sorgenti -stable direttamente
  dalla squadra per la sicurezza del kernel, e non passerà per il normale
  ciclo di revisione. Contattate la suddetta squadra per maggiori dettagli
  su questa procedura.

Sorgenti
--------

- La coda delle patch, sia quelle già applicate che in fase di revisione,
  possono essere trovate al seguente indirizzo:

    https://git.kernel.org/pub/scm/linux/kernel/git/stable/stable-queue.git

- Il rilascio definitivo, e marchiato, di tutti i kernel stabili può essere
  trovato in rami distinti per versione al seguente indirizzo:

    https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git

- I rilasci candidati di tutti i kernel stabili possono essere trovati al
  seguente indirizzo:

    https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable-rc.git/

  .. warning::
    I sorgenti -stable-rc sono un'istantanea dei sorgenti stable-queue e
    subirà frequenti modifiche, dunque verrà anche trapiantato spesso.
    Dovrebbe essere usato solo allo scopo di verifica (per esempio in un
    sistema di CI)

Comitato per la revisione
-------------------------

- Questo comitato è fatto di sviluppatori del kernel che si sono offerti
  volontari per questo lavoro, e pochi altri che non sono proprio volontari.
