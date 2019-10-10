.. include:: ../disclaimer-ita.rst

:Original: :ref:`Documentation/process/submit-checklist.rst <submitchecklist>`
:Translator: Federico Vaga <federico.vaga@vaga.pv.it>

.. _it_submitchecklist:

Lista delle verifiche da fare prima di inviare una patch per il kernel Linux
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Qui troverete una lista di cose che uno sviluppatore dovrebbe fare per
vedere le proprie patch accettate più rapidamente.

Tutti questi punti integrano la documentazione fornita riguardo alla
sottomissione delle patch, in particolare
:ref:`Documentation/translations/it_IT/process/submitting-patches.rst <it_submittingpatches>`.

1) Se state usando delle funzionalità del kernel allora includete (#include)
   i file che le dichiarano/definiscono.  Non dipendente dal fatto che un file
   d'intestazione include anche quelli usati da voi.

2) Compilazione pulita:

  a) con le opzioni ``CONFIG`` negli stati ``=y``, ``=m`` e ``=n``. Nessun
     avviso/errore di ``gcc`` e nessun avviso/errore dal linker.

  b) con ``allnoconfig``, ``allmodconfig``

  c) quando si usa ``O=builddir``

3) Compilare per diverse architetture di processore usando strumenti per
   la cross-compilazione o altri.

4) Una buona architettura per la verifica della cross-compilazione è la ppc64
   perché tende ad usare ``unsigned long`` per le quantità a 64-bit.

5) Controllate lo stile del codice della vostra patch secondo le direttive
   scritte in :ref:`Documentation/translations/it_IT/process/coding-style.rst <it_codingstyle>`.
   Prima dell'invio della patch, usate il verificatore di stile
   (``script/checkpatch.pl``) per scovare le violazioni più semplici.
   Dovreste essere in grado di giustificare tutte le violazioni rimanenti nella
   vostra patch.

6) Le opzioni ``CONFIG``, nuove o modificate, non scombussolano il menu
   di configurazione e sono preimpostate come disabilitate a meno che non
   soddisfino i criteri descritti in ``Documentation/kbuild/kconfig-language.rst``
   alla punto "Voci di menu: valori predefiniti".

7) Tutte le nuove opzioni ``Kconfig`` hanno un messaggio di aiuto.

8) La patch è stata accuratamente revisionata rispetto alle più importanti
   configurazioni ``Kconfig``.  Questo è molto difficile da fare
   correttamente - un buono lavoro di testa sarà utile.

9) Verificare con sparse.

10) Usare ``make checkstack`` e ``make namespacecheck`` e correggere tutti i
    problemi rilevati.

    .. note::

       ``checkstack`` non evidenzia esplicitamente i problemi, ma una funzione
       che usa più di 512 byte sullo stack è una buona candidata per una
       correzione.

11) Includete commenti :ref:`kernel-doc <kernel_doc>` per documentare API
    globali del kernel.  Usate ``make htmldocs`` o ``make pdfdocs`` per
    verificare i commenti :ref:`kernel-doc <kernel_doc>` ed eventualmente
    correggerli.

12) La patch è stata verificata con le seguenti opzioni abilitate
    contemporaneamente: ``CONFIG_PREEMPT``, ``CONFIG_DEBUG_PREEMPT``,
    ``CONFIG_DEBUG_SLAB``, ``CONFIG_DEBUG_PAGEALLOC``, ``CONFIG_DEBUG_MUTEXES``,
    ``CONFIG_DEBUG_SPINLOCK``, ``CONFIG_DEBUG_ATOMIC_SLEEP``,
    ``CONFIG_PROVE_RCU`` e ``CONFIG_DEBUG_OBJECTS_RCU_HEAD``.

13) La patch è stata compilata e verificata in esecuzione con, e senza,
    le opzioni ``CONFIG_SMP`` e ``CONFIG_PREEMPT``.

14) Se la patch ha effetti sull'IO dei dischi, eccetera: allora dev'essere
    verificata con, e senza, l'opzione ``CONFIG_LBDAF``.

15) Tutti i percorsi del codice sono stati verificati con tutte le funzionalità
    di lockdep abilitate.

16) Tutti i nuovi elementi in ``/proc`` sono documentati in ``Documentation/``.

17) Tutti i nuovi parametri d'avvio del kernel sono documentati in
    ``Documentation/admin-guide/kernel-parameters.rst``.

18) Tutti i nuovi parametri dei moduli sono documentati con ``MODULE_PARM_DESC()``.

19) Tutte le nuove interfacce verso lo spazio utente sono documentate in
    ``Documentation/ABI/``.  Leggete ``Documentation/ABI/README`` per maggiori
    informazioni.  Le patch che modificano le interfacce utente dovrebbero
    essere inviate in copia anche a linux-api@vger.kernel.org.

20) Verifica che il kernel passi con successo ``make headers_check``

21) La patch è stata verificata con l'iniezione di fallimenti in slab e
    nell'allocazione di pagine.  Vedere ``Documentation/fault-injection/``.

    Se il nuovo codice è corposo, potrebbe essere opportuno aggiungere
    l'iniezione di fallimenti specifici per il sottosistema.

22) Il nuovo codice è stato compilato con ``gcc -W`` (usate
    ``make EXTRA_CFLAGS=-W``).  Questo genererà molti avvisi, ma è ottimo
    per scovare bachi come  "warning: comparison between signed and unsigned".

23) La patch è stata verificata dopo essere stata inclusa nella serie di patch
    -mm; questo al fine di assicurarsi che continui a funzionare assieme a
    tutte le altre patch in coda e i vari cambiamenti nei sottosistemi VM, VFS
    e altri.

24) Tutte le barriere di sincronizzazione {per esempio, ``barrier()``,
    ``rmb()``, ``wmb()``} devono essere accompagnate da un commento nei
    sorgenti che ne spieghi la logica: cosa fanno e perché.

25) Se la patch aggiunge nuove chiamate ioctl, allora aggiornate
    ``Documentation/ioctl/ioctl-number.rst``.

26) Se il codice che avete modificato dipende o usa una qualsiasi interfaccia o
    funzionalità del kernel che è associata a uno dei seguenti simboli
    ``Kconfig``, allora verificate che il kernel compili con diverse
    configurazioni dove i simboli sono disabilitati e/o ``=m`` (se c'è la
    possibilità) [non tutti contemporaneamente, solo diverse combinazioni
    casuali]:

    ``CONFIG_SMP``, ``CONFIG_SYSFS``, ``CONFIG_PROC_FS``, ``CONFIG_INPUT``,
    ``CONFIG_PCI``, ``CONFIG_BLOCK``, ``CONFIG_PM``, ``CONFIG_MAGIC_SYSRQ``,
    ``CONFIG_NET``, ``CONFIG_INET=n`` (ma l'ultimo con ``CONFIG_NET=y``).
