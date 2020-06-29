.. include:: ../disclaimer-ita.rst

.. note:: Per leggere la documentazione originale in inglese:
	  :ref:`Documentation/doc-guide/index.rst <doc_guide>`

.. _it_kernel_doc:

Scrivere i commenti in kernel-doc
=================================

Nei file sorgenti del kernel Linux potrete trovare commenti di documentazione
strutturanti secondo il formato kernel-doc. Essi possono descrivere funzioni,
tipi di dati, e l'architettura del codice.

.. note:: Il formato kernel-doc può sembrare simile a gtk-doc o Doxygen ma
   in realtà è molto differente per ragioni storiche. I sorgenti del kernel
   contengono decine di migliaia di commenti kernel-doc. Siete pregati
   d'attenervi allo stile qui descritto.

La struttura kernel-doc è estratta a partire dai commenti; da questi viene
generato il `dominio Sphinx per il C`_ con un'adeguata descrizione per le
funzioni ed i tipi di dato con i loro relativi collegamenti. Le descrizioni
vengono filtrare per cercare i riferimenti ed i marcatori.

Vedere di seguito per maggiori dettagli.

.. _`dominio Sphinx per il C`: http://www.sphinx-doc.org/en/stable/domains.html

Tutte le funzioni esportate verso i moduli esterni utilizzando
``EXPORT_SYMBOL`` o ``EXPORT_SYMBOL_GPL`` dovrebbero avere un commento
kernel-doc. Quando l'intenzione è di utilizzarle nei moduli, anche le funzioni
e le strutture dati nei file d'intestazione dovrebbero avere dei commenti
kernel-doc.

È considerata una buona pratica quella di fornire una documentazione formattata
secondo kernel-doc per le funzioni che sono visibili da altri file del kernel
(ovvero, che non siano dichiarate utilizzando ``static``). Raccomandiamo,
inoltre, di fornire una documentazione kernel-doc anche per procedure private
(ovvero, dichiarate "static") al fine di fornire una struttura più coerente
dei sorgenti. Quest'ultima raccomandazione ha una priorità più bassa ed è a
discrezione dal manutentore (MAINTAINER) del file sorgente.



Sicuramente la documentazione formattata con kernel-doc è necessaria per
le funzioni che sono esportate verso i moduli esterni utilizzando
``EXPORT_SYMBOL`` o ``EXPORT_SYMBOL_GPL``.

Cerchiamo anche di fornire una documentazione formattata secondo kernel-doc
per le funzioni che sono visibili da altri file del kernel (ovvero, che non
siano dichiarate utilizzando "static")

Raccomandiamo, inoltre, di fornire una documentazione formattata con kernel-doc
anche per procedure private (ovvero, dichiarate "static") al fine di fornire
una struttura più coerente dei sorgenti. Questa raccomandazione ha una priorità
più bassa ed è a discrezione dal manutentore (MAINTAINER) del file sorgente.

Le strutture dati visibili nei file di intestazione dovrebbero essere anch'esse
documentate utilizzando commenti formattati con kernel-doc.

Come formattare i commenti kernel-doc
-------------------------------------

I commenti kernel-doc iniziano con il marcatore ``/**``. Il programma
``kernel-doc`` estrarrà i commenti marchiati in questo modo. Il resto
del commento è formattato come un normale commento multilinea, ovvero
con un asterisco all'inizio d'ogni riga e che si conclude con ``*/``
su una riga separata.

I commenti kernel-doc di funzioni e tipi dovrebbero essere posizionati
appena sopra la funzione od il tipo che descrivono. Questo allo scopo di
aumentare la probabilità che chi cambia il codice si ricordi di aggiornare
anche la documentazione. I commenti kernel-doc di tipo più generale possono
essere posizionati ovunque nel file.

Al fine di verificare che i commenti siano formattati correttamente, potete
eseguire il programma ``kernel-doc`` con un livello di verbosità alto e senza
che questo produca alcuna documentazione. Per esempio::

	scripts/kernel-doc -v -none drivers/foo/bar.c

Il formato della documentazione è verificato della procedura di generazione
del kernel quando viene richiesto di effettuare dei controlli extra con GCC::

	make W=n

Documentare le funzioni
------------------------

Generalmente il formato di un commento kernel-doc per funzioni e
macro simil-funzioni è il seguente::

  /**
   * function_name() - Brief description of function.
   * @arg1: Describe the first argument.
   * @arg2: Describe the second argument.
   *        One can provide multiple line descriptions
   *        for arguments.
   *
   * A longer description, with more discussion of the function function_name()
   * that might be useful to those using or modifying it. Begins with an
   * empty comment line, and may include additional embedded empty
   * comment lines.
   *
   * The longer description may have multiple paragraphs.
   *
   * Context: Describes whether the function can sleep, what locks it takes,
   *          releases, or expects to be held. It can extend over multiple
   *          lines.
   * Return: Describe the return value of function_name.
   *
   * The return value description can also have multiple paragraphs, and should
   * be placed at the end of the comment block.
   */

La descrizione introduttiva (*brief description*) che segue il nome della
funzione può continuare su righe successive e termina con la descrizione di
un argomento, una linea di commento vuota, oppure la fine del commento.

Parametri delle funzioni
~~~~~~~~~~~~~~~~~~~~~~~~

Ogni argomento di una funzione dovrebbe essere descritto in ordine, subito
dopo la descrizione introduttiva.  Non lasciare righe vuote né fra la
descrizione introduttiva e quella degli argomenti, né fra gli argomenti.

Ogni ``@argument:`` può estendersi su più righe.

.. note::

   Se la descrizione di ``@argument:`` si estende su più righe,
   la continuazione dovrebbe iniziare alla stessa colonna della riga
   precedente::

      * @argument: some long description
      *            that continues on next lines

   or::

      * @argument:
      *		some long description
      *		that continues on next lines

Se una funzione ha un numero variabile di argomento, la sua descrizione
dovrebbe essere scritta con la notazione kernel-doc::

      * @...: description

Contesto delle funzioni
~~~~~~~~~~~~~~~~~~~~~~~

Il contesto in cui le funzioni vengono chiamate viene descritto in una
sezione chiamata ``Context``. Questo dovrebbe informare sulla possibilità
che una funzione dorma (*sleep*) o che possa essere chiamata in un contesto
d'interruzione, così come i *lock* che prende, rilascia e che si aspetta che
vengano presi dal chiamante.

Esempi::

  * Context: Any context.
  * Context: Any context. Takes and releases the RCU lock.
  * Context: Any context. Expects <lock> to be held by caller.
  * Context: Process context. May sleep if @gfp flags permit.
  * Context: Process context. Takes and releases <mutex>.
  * Context: Softirq or process context. Takes and releases <lock>, BH-safe.
  * Context: Interrupt context.

Valore di ritorno
~~~~~~~~~~~~~~~~~

Il valore di ritorno, se c'è, viene descritto in una sezione dedicata di nome
``Return``.

.. note::

  #) La descrizione multiriga non riconosce il termine d'una riga, per cui
     se provate a formattare bene il vostro testo come nel seguente esempio::

	* Return:
	* 0 - OK
	* -EINVAL - invalid argument
	* -ENOMEM - out of memory

     le righe verranno unite e il risultato sarà::

	Return: 0 - OK -EINVAL - invalid argument -ENOMEM - out of memory

     Quindi, se volete che le righe vengano effettivamente generate, dovete
     utilizzare una lista ReST, ad esempio::

      * Return:
      * * 0		- OK to runtime suspend the device
      * * -EBUSY	- Device should not be runtime suspended

  #) Se il vostro testo ha delle righe che iniziano con una frase seguita dai
     due punti, allora ognuna di queste frasi verrà considerata come il nome
     di una nuova sezione, e probabilmente non produrrà gli effetti desiderati.

Documentare strutture, unioni ed enumerazioni
---------------------------------------------

Generalmente il formato di un commento kernel-doc per struct, union ed enum è::

  /**
   * struct struct_name - Brief description.
   * @member1: Description of member1.
   * @member2: Description of member2.
   *           One can provide multiple line descriptions
   *           for members.
   *
   * Description of the structure.
   */

Nell'esempio qui sopra, potete sostituire ``struct`` con ``union`` o ``enum``
per descrivere unioni ed enumerati. ``member`` viene usato per indicare i
membri di strutture ed unioni, ma anche i valori di un tipo enumerato.

La descrizione introduttiva (*brief description*) che segue il nome della
funzione può continuare su righe successive e termina con la descrizione di
un argomento, una linea di commento vuota, oppure la fine del commento.

Membri
~~~~~~

I membri di strutture, unioni ed enumerati devo essere documentati come i
parametri delle funzioni; seguono la descrizione introduttiva e possono
estendersi su più righe.

All'interno d'una struttura o d'un unione, potete utilizzare le etichette
``private:`` e ``public:``. I campi che sono nell'area ``private:`` non
verranno inclusi nella documentazione finale.

Le etichette ``private:`` e ``public:`` devono essere messe subito dopo
il marcatore di un commento ``/*``. Opzionalmente, possono includere commenti
fra ``:`` e il marcatore di fine commento ``*/``.

Esempio::

  /**
   * struct my_struct - short description
   * @a: first member
   * @b: second member
   * @d: fourth member
   *
   * Longer description
   */
  struct my_struct {
      int a;
      int b;
  /* private: internal use only */
      int c;
  /* public: the next one is public */
      int d;
  };

Strutture ed unioni annidate
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

È possibile documentare strutture ed unioni annidate, ad esempio::

      /**
       * struct nested_foobar - a struct with nested unions and structs
       * @memb1: first member of anonymous union/anonymous struct
       * @memb2: second member of anonymous union/anonymous struct
       * @memb3: third member of anonymous union/anonymous struct
       * @memb4: fourth member of anonymous union/anonymous struct
       * @bar: non-anonymous union
       * @bar.st1: struct st1 inside @bar
       * @bar.st2: struct st2 inside @bar
       * @bar.st1.memb1: first member of struct st1 on union bar
       * @bar.st1.memb2: second member of struct st1 on union bar
       * @bar.st2.memb1: first member of struct st2 on union bar
       * @bar.st2.memb2: second member of struct st2 on union bar
       */
      struct nested_foobar {
        /* Anonymous union/struct*/
        union {
          struct {
            int memb1;
            int memb2;
        }
          struct {
            void *memb3;
            int memb4;
          }
        }
        union {
          struct {
            int memb1;
            int memb2;
          } st1;
          struct {
            void *memb1;
            int memb2;
          } st2;
        } bar;
      };

.. note::

   #) Quando documentate una struttura od unione annidata, ad esempio
      di nome ``foo``, il suo campo ``bar`` dev'essere documentato
      usando ``@foo.bar:``
   #) Quando la struttura od unione annidata è anonima, il suo campo
      ``bar`` dev'essere documentato usando ``@bar:``

Commenti in linea per la documentazione dei membri
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

I membri d'una struttura possono essere documentati in linea all'interno
della definizione stessa. Ci sono due stili: una singola riga di commento
che inizia con ``/**`` e finisce con ``*/``; commenti multi riga come
qualsiasi altro commento kernel-doc::

  /**
   * struct foo - Brief description.
   * @foo: The Foo member.
   */
  struct foo {
        int foo;
        /**
         * @bar: The Bar member.
         */
        int bar;
        /**
         * @baz: The Baz member.
         *
         * Here, the member description may contain several paragraphs.
         */
        int baz;
        union {
                /** @foobar: Single line description. */
                int foobar;
        };
        /** @bar2: Description for struct @bar2 inside @foo */
        struct {
                /**
                 * @bar2.barbar: Description for @barbar inside @foo.bar2
                 */
                int barbar;
        } bar2;
  };


Documentazione dei tipi di dato
-------------------------------
Generalmente il formato di un commento kernel-doc per typedef è
il seguente::

  /**
   * typedef type_name - Brief description.
   *
   * Description of the type.
   */

Anche i tipi di dato per prototipi di funzione possono essere documentati::

  /**
   * typedef type_name - Brief description.
   * @arg1: description of arg1
   * @arg2: description of arg2
   *
   * Description of the type.
   *
   * Context: Locking context.
   * Return: Meaning of the return value.
   */
   typedef void (*type_name)(struct v4l2_ctrl *arg1, void *arg2);

Marcatori e riferimenti
-----------------------

All'interno dei commenti di tipo kernel-doc vengono riconosciuti i seguenti
*pattern* che vengono convertiti in marcatori reStructuredText ed in riferimenti
del `dominio Sphinx per il C`_.

.. attention:: Questi sono riconosciuti **solo** all'interno di commenti
               kernel-doc, e **non** all'interno di documenti reStructuredText.

``funcname()``
  Riferimento ad una funzione.

``@parameter``
  Nome di un parametro di una funzione (nessun riferimento, solo formattazione).

``%CONST``
  Il nome di una costante (nessun riferimento, solo formattazione)

````literal````
  Un blocco di testo che deve essere riportato così com'è. La rappresentazione
  finale utilizzerà caratteri a ``spaziatura fissa``.

  Questo è utile se dovete utilizzare caratteri speciali che altrimenti
  potrebbero assumere un significato diverso in kernel-doc o in reStructuredText

  Questo è particolarmente utile se dovete scrivere qualcosa come ``%ph``
  all'interno della descrizione di una funzione.

``$ENVVAR``
  Il nome di una variabile d'ambiente (nessun riferimento, solo formattazione).

``&struct name``
  Riferimento ad una struttura.

``&enum name``
  Riferimento ad un'enumerazione.

``&typedef name``
  Riferimento ad un tipo di dato.

``&struct_name->member`` or ``&struct_name.member``
  Riferimento ad un membro di una struttura o di un'unione. Il riferimento sarà
  la struttura o l'unione, non il memembro.

``&name``
  Un generico riferimento ad un tipo. Usate, preferibilmente, il riferimento
  completo come descritto sopra. Questo è dedicato ai commenti obsoleti.

Riferimenti usando reStructuredText
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Per fare riferimento a funzioni e tipi di dato definiti nei commenti kernel-doc
all'interno dei documenti reStructuredText, utilizzate i riferimenti dal
`dominio Sphinx per il C`_. Per esempio::

  See function :c:func:`foo` and struct/union/enum/typedef :c:type:`bar`.

Nonostante il riferimento ai tipi di dato funzioni col solo nome,
ovvero senza specificare struct/union/enum/typedef, potreste preferire il
seguente::

  See :c:type:`struct foo <foo>`.
  See :c:type:`union bar <bar>`.
  See :c:type:`enum baz <baz>`.
  See :c:type:`typedef meh <meh>`.

Questo produce dei collegamenti migliori, ed è in linea con il modo in cui
kernel-doc gestisce i riferimenti.

Per maggiori informazioni, siete pregati di consultare la documentazione
del `dominio Sphinx per il C`_.

Commenti per una documentazione generale
----------------------------------------

Al fine d'avere il codice ed i commenti nello stesso file, potete includere
dei blocchi di documentazione kernel-doc con un formato libero invece
che nel formato specifico per funzioni, strutture, unioni, enumerati o tipi
di dato. Per esempio, questo tipo di commento potrebbe essere usato per la
spiegazione delle operazioni di un driver o di una libreria

Questo s'ottiene utilizzando la parola chiave ``DOC:`` a cui viene associato
un titolo.

Generalmente il formato di un commento generico o di visione d'insieme è
il seguente::

  /**
   * DOC: Theory of Operation
   *
   * The whizbang foobar is a dilly of a gizmo. It can do whatever you
   * want it to do, at any time. It reads your mind. Here's how it works.
   *
   * foo bar splat
   *
   * The only drawback to this gizmo is that is can sometimes damage
   * hardware, software, or its subject(s).
   */

Il titolo che segue ``DOC:`` funziona da intestazione all'interno del file
sorgente, ma anche come identificatore per l'estrazione di questi commenti di
documentazione. Quindi, il titolo dev'essere unico all'interno del file.

Includere i commenti di tipo kernel-doc
=======================================

I commenti di documentazione possono essere inclusi in un qualsiasi documento
di tipo reStructuredText mediante l'apposita direttiva nell'estensione
kernel-doc per Sphinx.

Le direttive kernel-doc sono nel formato::

  .. kernel-doc:: source
     :option:

Il campo *source* è il percorso ad un file sorgente, relativo alla cartella
principale dei sorgenti del kernel. La direttiva supporta le seguenti opzioni:

export: *[source-pattern ...]*
  Include la documentazione per tutte le funzioni presenti nel file sorgente
  (*source*) che sono state esportate utilizzando ``EXPORT_SYMBOL`` o
  ``EXPORT_SYMBOL_GPL`` in *source* o in qualsiasi altro *source-pattern*
  specificato.

  Il campo *source-patter* è utile quando i commenti kernel-doc sono stati
  scritti nei file d'intestazione, mentre ``EXPORT_SYMBOL`` e
  ``EXPORT_SYMBOL_GPL`` si trovano vicino alla definizione delle funzioni.

  Esempi::

    .. kernel-doc:: lib/bitmap.c
       :export:

    .. kernel-doc:: include/net/mac80211.h
       :export: net/mac80211/*.c

internal: *[source-pattern ...]*
  Include la documentazione per tutte le funzioni ed i tipi presenti nel file
  sorgente (*source*) che **non** sono stati esportati utilizzando
  ``EXPORT_SYMBOL`` o ``EXPORT_SYMBOL_GPL`` né in *source* né in qualsiasi
  altro *source-pattern* specificato.

  Esempio::

    .. kernel-doc:: drivers/gpu/drm/i915/intel_audio.c
       :internal:

identifiers: *[ function/type ...]*
  Include la documentazione per ogni *function* e *type*  in *source*.
  Se non vengono esplicitamente specificate le funzioni da includere, allora
  verranno incluse tutte quelle disponibili in *source*.

  Esempi::

    .. kernel-doc:: lib/bitmap.c
       :identifiers: bitmap_parselist bitmap_parselist_user

    .. kernel-doc:: lib/idr.c
       :identifiers:

functions: *[ function ...]*
  Questo è uno pseudonimo, deprecato, per la direttiva 'identifiers'.

doc: *title*
  Include la documentazione del paragrafo ``DOC:`` identificato dal titolo
  (*title*) all'interno del file sorgente (*source*). Gli spazi in *title* sono
  permessi; non virgolettate *title*. Il campo *title* è utilizzato per
  identificare un paragrafo e per questo non viene incluso nella documentazione
  finale. Verificate d'avere l'intestazione appropriata nei documenti
  reStructuredText.

  Esempio::

    .. kernel-doc:: drivers/gpu/drm/i915/intel_audio.c
       :doc: High Definition Audio over HDMI and Display Port

Senza alcuna opzione, la direttiva kernel-doc include tutti i commenti di
documentazione presenti nel file sorgente (*source*).

L'estensione kernel-doc fa parte dei sorgenti del kernel, la si può trovare
in ``Documentation/sphinx/kerneldoc.py``. Internamente, viene utilizzato
lo script ``scripts/kernel-doc`` per estrarre i commenti di documentazione
dai file sorgenti.

Come utilizzare kernel-doc per generare pagine man
--------------------------------------------------

Se volete utilizzare kernel-doc solo per generare delle pagine man, potete
farlo direttamente dai sorgenti del kernel::

  $ scripts/kernel-doc -man $(git grep -l '/\*\*' -- :^Documentation :^tools) | scripts/split-man.pl /tmp/man
