.. include:: ../disclaimer-ita.rst

:Original: :ref:`Documentation/process/clang-format.rst <clangformat>`
:Translator: Federico Vaga <federico.vaga@vaga.pv.it>

.. _it_clangformat:

clang-format
============
``clang-format`` è uno strumento per formattare codice C/C++/... secondo
un gruppo di regole ed euristiche. Come tutti gli strumenti, non è perfetto
e non copre tutti i singoli casi, ma è abbastanza buono per essere utile.

``clang-format`` può essere usato per diversi fini:

  - Per riformattare rapidamente un blocco di codice secondo lo stile del
    kernel. Particolarmente utile quando si sposta del codice e lo si
    allinea/ordina. Vedere it_clangformatreformat_.

  - Identificare errori di stile, refusi e possibili miglioramenti nei
    file che mantieni, le modifiche che revisioni, le differenze,
    eccetera. Vedere it_clangformatreview_.

  - Ti aiuta a seguire lo stile del codice, particolarmente utile per i
    nuovi arrivati o per coloro che lavorano allo stesso tempo su diversi
    progetti con stili di codifica differenti.

Il suo file di configurazione è ``.clang-format`` e si trova nella cartella
principale dei sorgenti del kernel. Le regole scritte in quel file tentano
di approssimare le lo stile di codifica del kernel. Si tenta anche di seguire
il più possibile
:ref:`Documentation/translations/it_IT/process/coding-style.rst <it_codingstyle>`.
Dato che non tutto il kernel segue lo stesso stile, potreste voler aggiustare
le regole di base per un particolare sottosistema o cartella. Per farlo,
potete sovrascriverle scrivendole in un altro file ``.clang-format`` in
una sottocartella.

Questo strumento è già stato incluso da molto tempo nelle distribuzioni
Linux più popolari. Cercate ``clang-format`` nel vostro repositorio.
Altrimenti, potete scaricare una versione pre-generata dei binari di LLVM/clang
oppure generarlo dai codici sorgenti:

    http://releases.llvm.org/download.html

Troverete più informazioni ai seguenti indirizzi:

    https://clang.llvm.org/docs/ClangFormat.html

    https://clang.llvm.org/docs/ClangFormatStyleOptions.html


.. _it_clangformatreview:

Revisionare lo stile di codifica per file e modifiche
-----------------------------------------------------

Eseguendo questo programma, potrete revisionare un intero sottosistema,
cartella o singoli file alla ricerca di errori di stile, refusi o
miglioramenti.

Per farlo, potete eseguire qualcosa del genere::

    # Make sure your working directory is clean!
    clang-format -i kernel/*.[ch]

E poi date un'occhiata a *git diff*.

Osservare le righe di questo diff è utile a migliorare/aggiustare
le opzioni di stile nel file di configurazione; così come per verificare
le nuove funzionalità/versioni di ``clang-format``.

``clang-format`` è in grado di leggere diversi diff unificati, quindi
potrete revisionare facilmente delle modifiche e *git diff*.
La documentazione si trova al seguente indirizzo:

    https://clang.llvm.org/docs/ClangFormat.html#script-for-patch-reformatting

Per evitare che ``clang-format`` formatti alcune parti di un file, potete
scrivere nel codice::

    int formatted_code;
    // clang-format off
        void    unformatted_code  ;
    // clang-format on
    void formatted_code_again;

Nonostante si attraente l'idea di utilizzarlo per mantenere un file
sempre in sintonia con ``clang-format``, specialmente per file nuovi o
se siete un manutentore, ricordatevi che altre persone potrebbero usare
una versione diversa di ``clang-format`` oppure non utilizzarlo del tutto.
Quindi, dovreste trattenervi dall'usare questi marcatori nel codice del
kernel; almeno finché non vediamo che ``clang-format`` è diventato largamente
utilizzato.


.. _it_clangformatreformat:

Riformattare blocchi di codice
------------------------------

Utilizzando dei plugin per il vostro editor, potete riformattare una
blocco (selezione) di codice con una singola combinazione di tasti.
Questo è particolarmente utile: quando si riorganizza il codice, per codice
complesso, macro multi-riga (e allineare le loro "barre"), eccetera.

Ricordatevi che potete sempre aggiustare le modifiche in quei casi dove
questo strumento non ha fatto un buon lavoro. Ma come prima approssimazione,
può essere davvero molto utile.

Questo programma si integra con molti dei più popolari editor. Alcuni di
essi come vim, emacs, BBEdit, Visaul Studio, lo supportano direttamente.
Al seguente indirizzo troverete le istruzioni:

    https://clang.llvm.org/docs/ClangFormat.html

Per Atom, Eclipse, Sublime Text, Visual Studio Code, XCode e altri editor
e IDEs dovreste essere in grado di trovare dei plugin pronti all'uso.

Per questo caso d'uso, considerate l'uso di un secondo ``.clang-format``
che potete personalizzare con le vostre opzioni.
Consultare it_clangformatextra_.


.. _it_clangformatmissing:

Cose non supportate
-------------------

``clang-format`` non ha il supporto per alcune cose che sono comuni nel
codice del kernel. Sono facili da ricordare; quindi, se lo usate
regolarmente, imparerete rapidamente a evitare/ignorare certi problemi.

In particolare, quelli più comuni che noterete sono:

  - Allineamento di ``#define`` su una singola riga, per esempio::

        #define TRACING_MAP_BITS_DEFAULT       11
        #define TRACING_MAP_BITS_MAX           17
        #define TRACING_MAP_BITS_MIN           7

    contro::

        #define TRACING_MAP_BITS_DEFAULT 11
        #define TRACING_MAP_BITS_MAX 17
        #define TRACING_MAP_BITS_MIN 7

  - Allineamento dei valori iniziali, per esempio::

        static const struct file_operations uprobe_events_ops = {
                .owner          = THIS_MODULE,
                .open           = probes_open,
                .read           = seq_read,
                .llseek         = seq_lseek,
                .release        = seq_release,
                .write          = probes_write,
        };

    contro::

        static const struct file_operations uprobe_events_ops = {
                .owner = THIS_MODULE,
                .open = probes_open,
                .read = seq_read,
                .llseek = seq_lseek,
                .release = seq_release,
                .write = probes_write,
        };


.. _it_clangformatextra:

Funzionalità e opzioni aggiuntive
---------------------------------

Al fine di minimizzare le differenze fra il codice attuale e l'output
del programma, alcune opzioni di stile e funzionalità non sono abilitate
nella configurazione base. In altre parole, lo scopo è di rendere le
differenze le più piccole possibili, permettendo la semplificazione
della revisione di file, differenze e modifiche.

In altri casi (per esempio un particolare sottosistema/cartella/file), lo
stile del kernel potrebbe essere diverso e abilitare alcune di queste
opzioni potrebbe dare risultati migliori.

Per esempio:

  - Allineare assegnamenti (``AlignConsecutiveAssignments``).

  - Allineare dichiarazioni (``AlignConsecutiveDeclarations``).

  - Riorganizzare il testo nei commenti (``ReflowComments``).

  - Ordinare gli ``#include`` (``SortIncludes``).

Piuttosto che per interi file, solitamente sono utili per la riformattazione
di singoli blocchi. In alternativa, potete creare un altro file
``.clang-format`` da utilizzare con il vostro editor/IDE.
