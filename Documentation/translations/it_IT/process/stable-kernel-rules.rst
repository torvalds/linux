.. include:: ../disclaimer-ita.rst

:Original: :ref:`Documentation/process/stable-kernel-rules.rst <stable_kernel_rules>`
:Translator: Federico Vaga <federico.vaga@vaga.pv.it>

.. _it_stable_kernel_rules:

Tutto quello che volevate sapere sui rilasci -stable di Linux
==============================================================

Regole sul tipo di patch che vengono o non vengono accettate nei sorgenti
"-stable":

 - Ovviamente dev'essere corretta e verificata.
 - Non dev'essere più grande di 100 righe, incluso il contesto.
 - Deve correggere una cosa sola.
 - Deve correggere un baco vero che sta disturbando gli utenti (non cose del
   tipo "Questo potrebbe essere un problema ...").
 - Deve correggere un problema di compilazione (ma non per cose già segnate
   con CONFIG_BROKEN), un kernel oops, un blocco, una corruzione di dati,
   un vero problema di sicurezza, o problemi del tipo "oh, questo non va bene".
   In pratica, qualcosa di critico.
 - Problemi importanti riportati dagli utenti di una distribuzione potrebbero
   essere considerati se correggono importanti problemi di prestazioni o di
   interattività.  Dato che questi problemi non sono così ovvi e la loro
   correzione ha un'alta probabilità d'introdurre una regressione, dovrebbero
   essere sottomessi solo dal manutentore della distribuzione includendo un
   link, se esiste, ad un rapporto su bugzilla, e informazioni aggiuntive
   sull'impatto che ha sugli utenti.
 - Non deve correggere problemi relativi a una "teorica sezione critica",
   a meno che non venga fornita anche una spiegazione su come questa si
   possa verificare.
 - Non deve includere alcuna correzione "banale" (correzioni grammaticali,
   pulizia dagli spazi bianchi, eccetera).
 - Deve rispettare le regole scritte in
   :ref:`Documentation/translations/it_IT/process/submitting-patches.rst <it_submittingpatches>`
 - Questa patch o una equivalente deve esistere già nei sorgenti principali di
   Linux


Procedura per sottomettere patch per i sorgenti -stable
-------------------------------------------------------

.. note::
  Una patch di sicurezza non dovrebbe essere gestita (solamente) dal processo
  di revisione -stable, ma dovrebbe seguire le procedure descritte in
  :ref:`Documentation/translations/it_IT/admin-guide/security-bugs.rst <it_securitybugs>`.

Per tutte le altre sottomissioni, scegliere una delle seguenti procedure
------------------------------------------------------------------------

.. _it_option_1:

Opzione 1
*********

Per far sì che una patch venga automaticamente inclusa nei sorgenti stabili,
aggiungete l'etichetta

.. code-block:: none

     Cc: stable@vger.kernel.org

nell'area dedicata alla firme. Una volta che la patch è stata inclusa, verrà
applicata anche sui sorgenti stabili senza che l'autore o il manutentore
del sottosistema debba fare qualcosa.

.. _it_option_2:

Opzione 2
*********

Dopo che la patch è stata inclusa nei sorgenti Linux, inviate una mail a
stable@vger.kernel.org includendo: il titolo della patch, l'identificativo
del commit, il perché pensate che debba essere applicata, e in quale versione
del kernel la vorreste vedere.

.. _it_option_3:

Opzione 3
*********

Inviata la patch, dopo aver verificato che rispetta le regole descritte in
precedenza, a stable@vger.kernel.org.  Dovete annotare nel changelog
l'identificativo del commit nei sorgenti principali, così come la versione
del kernel nel quale vorreste vedere la patch.

L':ref:`it_option_1` è fortemente raccomandata; è il modo più facile e usato.
L':ref:`it_option_2` e l':ref:`it_option_3` sono più utili quando, al momento
dell'inclusione dei sorgenti principali, si ritiene che non debbano essere
incluse anche in quelli stabili (per esempio, perché si crede che si dovrebbero
fare più verifiche per eventuali regressioni). L':ref:`it_option_3` è
particolarmente utile se una patch dev'essere riportata su una versione
precedente (per esempio la patch richiede modifiche a causa di cambiamenti di
API).

Notate che per l':ref:`it_option_3`, se la patch è diversa da quella nei
sorgenti principali (per esempio perché è stato necessario un lavoro di
adattamento) allora dev'essere ben documentata e giustificata nella descrizione
della patch.

L'identificativo del commit nei sorgenti principali dev'essere indicato sopra
al messaggio della patch, così:

.. code-block:: none

    commit <sha1> upstream.

o in alternativa:

.. code-block:: none

    [ Upstream commit <sha1>  ]

In aggiunta, alcune patch inviate attraverso l':ref:`it_option_1` potrebbero
dipendere da altre che devo essere incluse. Questa situazione può essere
indicata nel seguente modo nell'area dedicata alle firme:

.. code-block:: none

     Cc: <stable@vger.kernel.org> # 3.3.x: a1f84a3: sched: Check for idle
     Cc: <stable@vger.kernel.org> # 3.3.x: 1b9508f: sched: Rate-limit newidle
     Cc: <stable@vger.kernel.org> # 3.3.x: fd21073: sched: Fix affinity logic
     Cc: <stable@vger.kernel.org> # 3.3.x
     Signed-off-by: Ingo Molnar <mingo@elte.hu>

La sequenza di etichette ha il seguente significato:

.. code-block:: none

     git cherry-pick a1f84a3
     git cherry-pick 1b9508f
     git cherry-pick fd21073
     git cherry-pick <this commit>

Inoltre, alcune patch potrebbero avere dei requisiti circa la versione del
kernel. Questo può essere indicato usando il seguente formato nell'area
dedicata alle firme:

.. code-block:: none

     Cc: <stable@vger.kernel.org> # 3.3.x

L'etichetta ha il seguente significato:

.. code-block:: none

     git cherry-pick <this commit>

per ogni sorgente "-stable" che inizia con la versione indicata.

Dopo la sottomissione:

 - Il mittente riceverà un ACK quando la patch è stata accettata e messa in
   coda, oppure un NAK se la patch è stata rigettata.  A seconda degli impegni
   degli sviluppatori, questa risposta potrebbe richiedere alcuni giorni.
 - Se accettata, la patch verrà aggiunta alla coda -stable per essere
   revisionata dal altri sviluppatori e dal principale manutentore del
   sottosistema.


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
