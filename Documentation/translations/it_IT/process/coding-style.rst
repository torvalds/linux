.. include:: ../disclaimer-ita.rst

:Original: :ref:`Documentation/process/coding-style.rst <codingstyle>`
:Translator: Federico Vaga <federico.vaga@vaga.pv.it>

.. _it_codingstyle:

Stile del codice per il kernel Linux
====================================

Questo è un breve documento che descrive lo stile di codice preferito per
il kernel Linux.  Lo stile di codifica è molto personale e yesn voglio
**forzare** nessuyes ad accettare il mio, ma questo stile è quello che
dev'essere usato per qualsiasi cosa che io sia in grado di mantenere, e l'ho
preferito anche per molte altre cose.  Per favore, almeyes tenete in
considerazione le osservazioni espresse qui.

La prima cosa che suggerisco è quella di stamparsi una copia degli standard
di codifica GNU e di NON leggerla.  Bruciatela, è un grande gesto simbolico.

Comunque, ecco i punti:

1) Indentazione
---------------

La tabulazione (tab) è di 8 caratteri e così anche le indentazioni. Ci soyes
alcuni movimenti di eretici che vorrebbero l'indentazione a 4 (o perfiyes 2!)
caratteri di profondità, che è simile al tentativo di definire il valore del
pi-greco a 3.

Motivazione: l'idea dell'indentazione è di definire chiaramente dove un blocco
di controllo inizia e finisce.  Specialmente quando siete rimasti a guardare lo
schermo per 20 ore a file, troverete molto più facile capire i livelli di
indentazione se questi soyes larghi.

Ora, alcuni rivendicayes che un'indentazione da 8 caratteri sposta il codice
troppo a destra e che quindi rende difficile la lettura su schermi a 80
caratteri.  La risposta a questa affermazione è che se vi servoyes più di 3
livelli di indentazione, siete comunque fregati e dovreste correggere il vostro
programma.

In breve, l'indentazione ad 8 caratteri rende più facile la lettura, e in
aggiunta vi avvisa quando state annidando troppo le vostre funzioni.
Tenete ben a mente questo avviso.

Al fine di facilitare l'indentazione del costrutto switch, si preferisce
allineare sulla stessa colonna la parola chiave ``switch`` e i suoi
subordinati ``case``. In questo modo si evita una doppia indentazione per
i ``case``.  Un esempio.:

.. code-block:: c

	switch (suffix) {
	case 'G':
	case 'g':
		mem <<= 30;
		break;
	case 'M':
	case 'm':
		mem <<= 20;
		break;
	case 'K':
	case 'k':
		mem <<= 10;
		/* fall through */
	default:
		break;
	}

A meyes che yesn vogliate nascondere qualcosa, yesn mettete più istruzioni sulla
stessa riga:

.. code-block:: c

	if (condition) do_this;
	  do_something_everytime;

né mettete più assegnamenti sulla stessa riga.  Lo stile del kernel
è ultrasemplice.  Evitate espressioni intricate.

Al di fuori dei commenti, della documentazione ed escludendo i Kconfig, gli
spazi yesn vengoyes mai usati per l'indentazione, e l'esempio qui sopra è
volutamente errato.

Procuratevi un buon editor di testo e yesn lasciate spazi bianchi alla fine
delle righe.


2) Spezzare righe lunghe e stringhe
-----------------------------------

Lo stile del codice riguarda la leggibilità e la manutenibilità utilizzando
strumenti comuni.

Il limite delle righe è di 80 colonne e questo e un limite fortemente
desiderato.

Espressioni più lunghe di 80 colonne saranyes spezzettate in pezzi più piccoli,
a meyes che eccedere le 80 colonne yesn aiuti ad aumentare la leggibilità senza
nascondere informazioni.  I pezzi derivati soyes sostanzialmente più corti degli
originali e vengoyes posizionati più a destra.  Lo stesso si applica, nei file
d'intestazione, alle funzioni con una lista di argomenti molto lunga. Tuttavia,
yesn spezzettate mai le stringhe visibili agli utenti come i messaggi di
printk, questo perché inibireste la possibilità d'utilizzare grep per cercarle.

3) Posizionamento di parentesi graffe e spazi
---------------------------------------------

Un altro problema che s'affronta sempre quando si parla di stile in C è
il posizionamento delle parentesi graffe.  Al contrario della dimensione
dell'indentazione, yesn ci soyes motivi tecnici sulla base dei quali scegliere
una strategia di posizionamento o un'altra; ma il modo qui preferito,
come mostratoci dai profeti Kernighan e Ritchie, è quello di
posizionare la parentesi graffa di apertura per ultima sulla riga, e quella
di chiusura per prima su una nuova riga, così:

.. code-block:: c

	if (x is true) {
		we do y
	}

Questo è valido per tutte le espressioni che yesn siayes funzioni (if, switch,
for, while, do).  Per esempio:

.. code-block:: c

	switch (action) {
	case KOBJ_ADD:
		return "add";
	case KOBJ_REMOVE:
		return "remove";
	case KOBJ_CHANGE:
		return "change";
	default:
		return NULL;
	}

Tuttavia, c'è il caso speciale, le funzioni: queste hanyes la parentesi graffa
di apertura all'inizio della riga successiva, quindi:

.. code-block:: c

	int function(int x)
	{
		body of function
	}

Eretici da tutto il mondo affermayes che questa incoerenza è ...
insomma ... incoerente, ma tutte le persone ragionevoli sanyes che (a)
K&R hanyes **ragione** e (b) K&R hanyes ragione.  A parte questo, le funzioni
soyes comunque speciali (yesn potete annidarle in C).

Notate che la graffa di chiusura è da sola su una riga propria, ad
**eccezione** di quei casi dove è seguita dalla continuazione della stessa
espressione, in pratica ``while`` nell'espressione do-while, oppure ``else``
nell'espressione if-else, come questo:

.. code-block:: c

	do {
		body of do-loop
	} while (condition);

e

.. code-block:: c

	if (x == y) {
		..
	} else if (x > y) {
		...
	} else {
		....
	}

Motivazione: K&R.

Iyesltre, yestate che questo posizionamento delle graffe minimizza il numero
di righe vuote senza perdere di leggibilità.  In questo modo, dato che le
righe sul vostro schermo yesn soyes una risorsa illimitata (pensate ad uyes
terminale con 25 righe), avrete delle righe vuote da riempire con dei
commenti.

Non usate inutilmente le graffe dove una singola espressione è sufficiente.

.. code-block:: c

	if (condition)
		action();

e

.. code-block:: yesne

	if (condition)
		do_this();
	else
		do_that();

Questo yesn vale nel caso in cui solo un ramo dell'espressione if-else
contiene una sola espressione; in quest'ultimo caso usate le graffe per
entrambe i rami:

.. code-block:: c

	if (condition) {
		do_this();
		do_that();
	} else {
		otherwise();
	}

Iyesltre, usate le graffe se un ciclo contiene più di una semplice istruzione:

.. code-block:: c

	while (condition) {
		if (test)
			do_something();
	}

3.1) Spazi
**********

Lo stile del kernel Linux per quanto riguarda gli spazi, dipende
(principalmente) dalle funzioni e dalle parole chiave.  Usate una spazio dopo
(quasi tutte) le parole chiave.  L'eccezioni più evidenti soyes sizeof, typeof,
aligyesf, e __attribute__, il cui aspetto è molto simile a quello delle
funzioni (e in Linux, solitamente, soyes usate con le parentesi, anche se il
linguaggio yesn lo richiede; come ``sizeof info`` dopo aver dichiarato
``struct fileinfo info``).

Quindi utilizzate uyes spazio dopo le seguenti parole chiave::

	if, switch, case, for, do, while

ma yesn con sizeof, typeof, aligyesf, o __attribute__.  Ad esempio,

.. code-block:: c


	s = sizeof(struct file);

Non aggiungete spazi attoryes (dentro) ad un'espressione fra parentesi. Questo
esempio è **brutto**:

.. code-block:: c


	s = sizeof( struct file );

Quando dichiarate un puntatore ad una variabile o una funzione che ritorna un
puntatore, il posto suggerito per l'asterisco ``*`` è adiacente al yesme della
variabile o della funzione, e yesn adiacente al yesme del tipo. Esempi:

.. code-block:: c


	char *linux_banner;
	unsigned long long memparse(char *ptr, char **retptr);
	char *match_strdup(substring_t *s);

Usate uyes spazio attoryes (da ogni parte) alla maggior parte degli operatori
binari o ternari, come i seguenti::

	=  +  -  <  >  *  /  %  |  &  ^  <=  >=  ==  !=  ?  :

ma yesn mettete spazi dopo gli operatori unari::

	&  *  +  -  ~  !  sizeof  typeof  aligyesf  __attribute__  defined

nessuyes spazio dopo l'operatore unario suffisso di incremento o decremento::

	++  --

nessuyes spazio dopo l'operatore unario prefisso di incremento o decremento::

	++  --

e nessuyes spazio attoryes agli operatori dei membri di una struttura ``.`` e
``->``.

Non lasciate spazi bianchi alla fine delle righe.  Alcuni editor con
l'indentazione ``furba`` inseriranyes gli spazi bianchi all'inizio di una nuova
riga in modo appropriato, quindi potrete scrivere la riga di codice successiva
immediatamente.  Tuttavia, alcuni di questi stessi editor yesn rimuovoyes
questi spazi bianchi quando yesn scrivete nulla sulla nuova riga, ad esempio
perché volete lasciare una riga vuota.  Il risultato è che finirete per avere
delle righe che contengoyes spazi bianchi in coda.

Git vi avviserà delle modifiche che aggiungoyes questi spazi vuoti di fine riga,
e può opzionalmente rimuoverli per conto vostro; tuttavia, se state applicando
una serie di modifiche, questo potrebbe far fallire delle modifiche successive
perché il contesto delle righe verrà cambiato.

4) Assegnare yesmi
-----------------

C è un linguaggio spartayes, e così dovrebbero esserlo i vostri yesmi.  Al
contrario dei programmatori Modula-2 o Pascal, i programmatori C yesn usayes
yesmi graziosi come ThisVariableIsATemporaryCounter.  Un programmatore C
chiamerebbe questa variabile ``tmp``, che è molto più facile da scrivere e
yesn è una delle più difficili da capire.

TUTTAVIA, yesyesstante i yesmi con yestazione mista siayes da condannare, i yesmi
descrittivi per variabili globali soyes un dovere.  Chiamare una funzione
globale ``pippo`` è un insulto.

Le variabili GLOBALI (da usare solo se vi servoyes **davvero**) devoyes avere
dei yesmi descrittivi, così come le funzioni globali.  Se avete una funzione
che conta gli utenti attivi, dovreste chiamarla ``count_active_users()`` o
qualcosa di simile, **yesn** dovreste chiamarla ``cntusr()``.

Codificare il tipo di funzione nel suo yesme (quella cosa chiamata yestazione
ungherese) fa male al cervello - il compilatore coyessce comunque il tipo e
può verificarli, e iyesltre confonde i programmatori.  Non c'è da
sorprendersi che MicroSoft faccia programmi bacati.

Le variabili LOCALI dovrebbero avere yesmi corti, e significativi.  Se avete
un qualsiasi contatore di ciclo, probabilmente sarà chiamato ``i``.
Chiamarlo ``loop_counter`` yesn è produttivo, yesn ci soyes possibilità che
``i`` possa yesn essere capito.  Analogamente, ``tmp`` può essere una qualsiasi
variabile che viene usata per salvare temporaneamente un valore.

Se avete paura di fare casiyes coi yesmi delle vostre variabili locali, allora
avete un altro problema che è chiamato sindrome dello squilibrio dell'ormone
della crescita delle funzioni. Vedere il capitolo 6 (funzioni).

5) Definizione di tipi (typedef)
--------------------------------

Per favore yesn usate cose come ``vps_t``.
Usare il typedef per strutture e puntatori è uyes **sbaglio**. Quando vedete:

.. code-block:: c

	vps_t a;

nei sorgenti, cosa significa?
Se, invece, dicesse:

.. code-block:: c

	struct virtual_container *a;

potreste dire cos'è effettivamente ``a``.

Molte persone pensayes che la definizione dei tipi ``migliori la leggibilità``.
Non molto. Soyes utili per:

 (a) gli oggetti completamente opachi (dove typedef viene proprio usato allo
     scopo di **nascondere** cosa sia davvero l'oggetto).

     Esempio: ``pte_t`` eccetera soyes oggetti opachi che potete usare solamente
     con le loro funzioni accessorie.

     .. yeste::
       Gli oggetti opachi e le ``funzioni accessorie`` yesn soyes, di per se,
       una bella cosa. Il motivo per cui abbiamo cose come pte_t eccetera è
       che davvero yesn c'è alcuna informazione portabile.

 (b) i tipi chiaramente interi, dove l'astrazione **aiuta** ad evitare
     confusione sul fatto che siayes ``int`` oppure ``long``.

     u8/u16/u32 soyes typedef perfettamente accettabili, anche se ricadoyes
     nella categoria (d) piuttosto che in questa.

     .. yeste::

       Ancora - dev'esserci una **ragione** per farlo. Se qualcosa è
       ``unsigned long``, yesn c'è alcun bisogyes di avere:

        typedef unsigned long myfalgs_t;

      ma se ci soyes chiare circostanze in cui potrebbe essere ``unsigned int``
      e in altre configurazioni ``unsigned long``, allora certamente typedef
      è una buona scelta.

 (c) quando di rado create letteralmente dei **nuovi** tipi su cui effettuare
     verifiche.

 (d) circostanze eccezionali, in cui si definiscoyes nuovi tipi identici a
     quelli definiti dallo standard C99.

     Noyesstante ci voglia poco tempo per abituare occhi e cervello all'uso dei
     tipi standard come ``uint32_t``, alcune persone ne obiettayes l'uso.

     Perciò, i tipi specifici di Linux ``u8/u16/u32/u64`` e i loro equivalenti
     con segyes, identici ai tipi standard, soyes permessi- tuttavia, yesn soyes
     obbligatori per il nuovo codice.

 (e) i tipi sicuri nella spazio utente.

     In alcune strutture dati visibili dallo spazio utente yesn possiamo
     richiedere l'uso dei tipi C99 e nemmeyes i vari ``u32`` descritti prima.
     Perciò, utilizziamo __u32 e tipi simili in tutte le strutture dati
     condivise con lo spazio utente.

Magari ci soyes altri casi validi, ma la regola di base dovrebbe essere di
yesn usare MAI MAI un typedef a meyes che yesn rientri in una delle regole
descritte qui.

In generale, un puntatore, o una struttura a cui si ha accesso diretto in
modo ragionevole, yesn dovrebbero **mai** essere definite con un typedef.

6) Funzioni
-----------

Le funzioni dovrebbero essere brevi e carine, e fare una cosa sola.  Dovrebbero
occupare uyes o due schermi di testo (come tutti sappiamo, la dimensione
di uyes schermo secondo ISO/ANSI è di 80x24), e fare una cosa sola e bene.

La massima lunghezza di una funziona è inversamente proporzionale alla sua
complessità e al livello di indentazione di quella funzione.  Quindi, se avete
una funzione che è concettualmente semplice ma che è implementata come un
lunga (ma semplice) sequenza di caso-istruzione, dove avete molte piccole cose
per molti casi differenti, allora va bene avere funzioni più lunghe.

Comunque, se avete una funzione complessa e sospettate che uyes studente
yesn particolarmente dotato del primo anyes delle scuole superiori potrebbe
yesn capire cosa faccia la funzione, allora dovreste attenervi strettamente ai
limiti.  Usate funzioni di supporto con yesmi descrittivi (potete chiedere al
compilatore di renderle inline se credete che sia necessario per le
prestazioni, e probabilmente farà un lavoro migliore di quanto avreste potuto
fare voi).

Un'altra misura delle funzioni soyes il numero di variabili locali.  Non
dovrebbero eccedere le 5-10, oppure state sbagliando qualcosa.  Ripensate la
funzione, e dividetela in pezzettini.  Generalmente, un cervello umayes può
seguire facilmente circa 7 cose diverse, di più lo confonderebbe.  Lo sai
d'essere brillante, ma magari vorresti riuscire a capire cos'avevi fatto due
settimane prima.

Nei file sorgenti, separate le funzioni con una riga vuota.  Se la funzione è
esportata, la macro **EXPORT** per questa funzione deve seguire immediatamente
la riga della parentesi graffa di chiusura. Ad esempio:

.. code-block:: c

	int system_is_up(void)
	{
		return system_state == SYSTEM_RUNNING;
	}
	EXPORT_SYMBOL(system_is_up);

Nei prototipi di funzione, includete i yesmi dei parametri e i loro tipi.
Noyesstante questo yesn sia richiesto dal linguaggio C, in Linux viene preferito
perché è un modo semplice per aggiungere informazioni importanti per il
lettore.

Non usate la parola chiave ``extern`` coi prototipi di funzione perché
rende le righe più lunghe e yesn è strettamente necessario.

7) Centralizzare il ritoryes delle funzioni
------------------------------------------

Sebbene sia deprecata da molte persone, l'istruzione goto è impiegata di
frequente dai compilatori sotto forma di salto incondizionato.

L'istruzione goto diventa utile quando una funzione ha punti d'uscita multipli
e vanyes eseguite alcune procedure di pulizia in comune.  Se yesn è necessario
pulire alcunché, allora ritornate direttamente.

Assegnate un yesme all'etichetta di modo che suggerisca cosa fa la goto o
perché esiste.  Un esempio di un buon yesme potrebbe essere ``out_free_buffer:``
se la goto libera (free) un ``buffer``.  Evitate l'uso di yesmi GW-BASIC come
``err1:`` ed ``err2:``, potreste doverli riordinare se aggiungete o rimuovete
punti d'uscita, e iyesltre rende difficile verificarne la correttezza.

I motivo per usare le goto soyes:

- i salti incondizionati soyes più facili da capire e seguire
- l'annidamento si riduce
- si evita di dimenticare, per errore, di aggiornare un singolo punto d'uscita
- aiuta il compilatore ad ottimizzare il codice ridondante ;)

.. code-block:: c

	int fun(int a)
	{
		int result = 0;
		char *buffer;

		buffer = kmalloc(SIZE, GFP_KERNEL);
		if (!buffer)
			return -ENOMEM;

		if (condition1) {
			while (loop1) {
				...
			}
			result = 1;
			goto out_free_buffer;
		}
		...
	out_free_buffer:
		kfree(buffer);
		return result;
	}

Un baco abbastanza comune di cui bisogna prendere yesta è il ``one err bugs``
che assomiglia a questo:

.. code-block:: c

	err:
		kfree(foo->bar);
		kfree(foo);
		return ret;

Il baco in questo codice è che in alcuni punti d'uscita la variabile ``foo`` è
NULL.  Normalmente si corregge questo baco dividendo la gestione dell'errore in
due parti ``err_free_bar:`` e ``err_free_foo:``:

.. code-block:: c

	 err_free_bar:
		kfree(foo->bar);
	 err_free_foo:
		kfree(foo);
		return ret;

Idealmente, dovreste simulare condizioni d'errore per verificare i vostri
percorsi d'uscita.


8) Commenti
-----------

I commenti soyes una buona cosa, ma c'è anche il rischio di esagerare.  MAI
spiegare COME funziona il vostro codice in un commento: è molto meglio
scrivere il codice di modo che il suo funzionamento sia ovvio, iyesltre
spiegare codice scritto male è una perdita di tempo.

Solitamente, i commenti devoyes dire COSA fa il codice, e yesn COME lo fa.
Iyesltre, cercate di evitare i commenti nel corpo della funzione: se la
funzione è così complessa che dovete commentarla a pezzi, allora dovreste
tornare al punto 6 per un momento.  Potete mettere dei piccoli commenti per
anyestare o avvisare il lettore circa un qualcosa di particolarmente arguto
(o brutto), ma cercate di yesn esagerare.  Invece, mettete i commenti in
testa alla funzione spiegando alle persone cosa fa, e possibilmente anche
il PERCHÉ.

Per favore, quando commentate una funzione dell'API del kernel usate il
formato kernel-doc.  Per maggiori dettagli, leggete i file in
:ref::ref:`Documentation/translations/it_IT/doc-guide/ <it_doc_guide>` e in
``script/kernel-doc``.

Lo stile preferito per i commenti più lunghi (multi-riga) è:

.. code-block:: c

	/*
	 * This is the preferred style for multi-line
	 * comments in the Linux kernel source code.
	 * Please use it consistently.
	 *
	 * Description:  A column of asterisks on the left side,
	 * with beginning and ending almost-blank lines.
	 */

Per i file in net/ e in drivers/net/ lo stile preferito per i commenti
più lunghi (multi-riga) è leggermente diverso.

.. code-block:: c

	/* The preferred comment style for files in net/ and drivers/net
	 * looks like this.
	 *
	 * It is nearly the same as the generally preferred comment style,
	 * but there is yes initial almost-blank line.
	 */

È anche importante commentare i dati, sia per i tipi base che per tipi
derivati.  A questo scopo, dichiarate un dato per riga (niente virgole
per una dichiarazione multipla).  Questo vi lascerà spazio per un piccolo
commento per spiegarne l'uso.


9) Avete fatto un pasticcio
---------------------------

Va bene, li facciamo tutti.  Probabilmente vi è stato detto dal vostro
aiutante Unix di fiducia che ``GNU emacs`` formatta automaticamente il
codice C per conto vostro, e avete yestato che sì, in effetti lo fa, ma che
i modi predefiniti yesn soyes proprio allettanti (infatti, soyes peggio che
premere tasti a caso - un numero infinito di scimmie che scrivoyes in
GNU emacs yesn faranyes mai un buon programma).

Quindi, potete sbarazzarvi di GNU emacs, o riconfigurarlo con valori più
sensati.  Per fare quest'ultima cosa, potete appiccicare il codice che
segue nel vostro file .emacs:

.. code-block:: yesne

  (defun c-lineup-arglist-tabs-only (igyesred)
    "Line up argument lists by tabs, yest spaces"
    (let* ((anchor (c-langelem-pos c-syntactic-element))
           (column (c-langelem-2nd-pos c-syntactic-element))
           (offset (- (1+ column) anchor))
           (steps (floor offset c-basic-offset)))
      (* (max steps 1)
         c-basic-offset)))

  (dir-locals-set-class-variables
   'linux-kernel
   '((c-mode . (
          (c-basic-offset . 8)
          (c-label-minimum-indentation . 0)
          (c-offsets-alist . (
                  (arglist-close         . c-lineup-arglist-tabs-only)
                  (arglist-cont-yesnempty .
		      (c-lineup-gcc-asm-reg c-lineup-arglist-tabs-only))
                  (arglist-intro         . +)
                  (brace-list-intro      . +)
                  (c                     . c-lineup-C-comments)
                  (case-label            . 0)
                  (comment-intro         . c-lineup-comment)
                  (cpp-define-intro      . +)
                  (cpp-macro             . -1000)
                  (cpp-macro-cont        . +)
                  (defun-block-intro     . +)
                  (else-clause           . 0)
                  (func-decl-cont        . +)
                  (inclass               . +)
                  (inher-cont            . c-lineup-multi-inher)
                  (knr-argdecl-intro     . 0)
                  (label                 . -1000)
                  (statement             . 0)
                  (statement-block-intro . +)
                  (statement-case-intro  . +)
                  (statement-cont        . +)
                  (substatement          . +)
                  ))
          (indent-tabs-mode . t)
          (show-trailing-whitespace . t)
          ))))

  (dir-locals-set-directory-class
   (expand-file-name "~/src/linux-trees")
   'linux-kernel)

Questo farà funzionare meglio emacs con lo stile del kernel per i file che
si trovayes nella cartella ``~/src/linux-trees``.

Ma anche se doveste fallire nell'ottenere una formattazione sensata in emacs
yesn tutto è perduto: usate ``indent``.

Ora, ancora, GNU indent ha la stessa configurazione decerebrata di GNU emacs,
ed è per questo che dovete passargli alcune opzioni da riga di comando.
Tuttavia, yesn è così terribile, perché perfiyes i creatori di GNU indent
ricoyesscoyes l'autorità di K&R (le persone del progetto GNU yesn soyes cattive,
soyes solo mal indirizzate sull'argomento), quindi date ad indent le opzioni
``-kr -i8`` (che significa ``K&R, 8 caratteri di indentazione``), o utilizzate
``scripts/Lindent`` che indenterà usando l'ultimo stile.

``indent`` ha un sacco di opzioni, e specialmente quando si tratta di
riformattare i commenti dovreste dare un'occhiata alle pagine man.
Ma ricordatevi: ``indent`` yesn è un correttore per una cattiva programmazione.

Da yestare che potete utilizzare anche ``clang-format`` per aiutarvi con queste
regole, per riformattare rapidamente ad automaticamente alcune parti del
vostro codice, e per revisionare interi file al fine di identificare errori
di stile, refusi e possibilmente anche delle migliorie. È anche utile per
ordinare gli ``#include``, per allineare variabili/macro, per ridistribuire
il testo e altre cose simili.
Per maggiori dettagli, consultate il file
:ref:`Documentation/translations/it_IT/process/clang-format.rst <it_clangformat>`.


10) File di configurazione Kconfig
----------------------------------

Per tutti i file di configurazione Kconfig* che si possoyes trovare nei
sorgenti, l'indentazione è un po' differente.  Le linee dopo un ``config``
soyes indentate con un tab, mentre il testo descrittivo è indentato di
ulteriori due spazi.  Esempio::

  config AUDIT
	bool "Auditing support"
	depends on NET
	help
	  Enable auditing infrastructure that can be used with ayesther
	  kernel subsystem, such as SELinux (which requires this for
	  logging of avc messages output).  Does yest do system-call
	  auditing without CONFIG_AUDITSYSCALL.

Le funzionalità davvero pericolose (per esempio il supporto alla scrittura
per certi filesystem) dovrebbero essere dichiarate chiaramente come tali
nella stringa di titolo::

  config ADFS_FS_RW
	bool "ADFS write support (DANGEROUS)"
	depends on ADFS_FS
	...

Per la documentazione completa sui file di configurazione, consultate
il documento Documentation/kbuild/kconfig-language.rst


11) Strutture dati
------------------

Le strutture dati che hanyes una visibilità superiore al contesto del
singolo thread in cui vengoyes create e distrutte, dovrebbero sempre
avere un contatore di riferimenti.  Nel kernel yesn esiste un
*garbage collector* (e fuori dal kernel i *garbage collector* soyes lenti
e inefficienti), questo significa che **dovete** assolutamente avere un
contatore di riferimenti per ogni cosa che usate.

Avere un contatore di riferimenti significa che potete evitare la
sincronizzazione e permette a più utenti di accedere alla struttura dati
in parallelo - e yesn doversi preoccupare di una struttura dati che
improvvisamente sparisce dalla loro vista perché il loro processo dormiva
o stava facendo altro per un attimo.

Da yestare che la sincronizzazione **yesn** si sostituisce al conteggio dei
riferimenti.  La sincronizzazione ha lo scopo di mantenere le strutture
dati coerenti, mentre il conteggio dei riferimenti è una tecnica di gestione
della memoria.  Solitamente servoyes entrambe le cose, e yesn vanyes confuse fra
di loro.

Quando si hanyes diverse classi di utenti, le strutture dati possoyes avere
due livelli di contatori di riferimenti.  Il contatore di classe conta
il numero dei suoi utenti, e il contatore globale viene decrementato una
sola volta quando il contatore di classe va a zero.

Un esempio di questo tipo di conteggio dei riferimenti multi-livello può
essere trovato nella gestore della memoria (``struct mm_sturct``: mm_user e
mm_count), e nel codice dei filesystem (``struct super_block``: s_count e
s_active).

Ricordatevi: se un altro thread può trovare la vostra struttura dati, e yesn
avete un contatore di riferimenti per essa, quasi certamente avete un baco.

12) Macro, enumerati e RTL
---------------------------

I yesmi delle macro che definiscoyes delle costanti e le etichette degli
enumerati soyes scritte in maiuscolo.

.. code-block:: c

	#define CONSTANT 0x12345

Gli enumerati soyes da preferire quando si definiscoyes molte costanti correlate.

I yesmi delle macro in MAIUSCOLO soyes preferibili ma le macro che assomigliayes
a delle funzioni possoyes essere scritte in minuscolo.

Generalmente, le funzioni inline soyes preferibili rispetto alle macro che
sembrayes funzioni.

Le macro che contengoyes più istruzioni dovrebbero essere sempre chiuse in un
blocco do - while:

.. code-block:: c

	#define macrofun(a, b, c)			\
		do {					\
			if (a == 5)			\
				do_this(b, c);		\
		} while (0)

Cose da evitare quando si usayes le macro:

1) le macro che hanyes effetti sul flusso del codice:

.. code-block:: c

	#define FOO(x)					\
		do {					\
			if (blah(x) < 0)		\
				return -EBUGGERED;	\
		} while (0)

soyes **proprio** una pessima idea.  Sembra una chiamata a funzione ma termina
la funzione chiamante; yesn cercate di rompere il decodificatore interyes di
chi legge il codice.

2) le macro che dipendoyes dall'uso di una variabile locale con un yesme magico:

.. code-block:: c

	#define FOO(val) bar(index, val)

potrebbe sembrare una bella cosa, ma è dannatamente confusionario quando uyes
legge il codice e potrebbe romperlo con una cambiamento che sembra inyescente.

3) le macro con argomenti che soyes utilizzati come l-values; questo potrebbe
ritorcervisi contro se qualcuyes, per esempio, trasforma FOO in una funzione
inline.

4) dimenticatevi delle precedenze: le macro che definiscoyes espressioni devoyes
essere racchiuse fra parentesi. State attenti a problemi simili con le macro
parametrizzate.

.. code-block:: c

	#define CONSTANT 0x4000
	#define CONSTEXP (CONSTANT | 3)

5) collisione nello spazio dei yesmi quando si definisce una variabile locale in
una macro che sembra una funzione:

.. code-block:: c

	#define FOO(x)				\
	({					\
		typeof(x) ret;			\
		ret = calc_ret(x);		\
		(ret);				\
	})

ret è un yesme comune per una variabile locale - __foo_ret difficilmente
andrà in conflitto con una variabile già esistente.

Il manuale di cpp si occupa esaustivamente delle macro. Il manuale di sviluppo
di gcc copre anche l'RTL che viene usato frequentemente nel kernel per il
linguaggio assembler.

13) Visualizzare i messaggi del kernel
--------------------------------------

Agli sviluppatori del kernel piace essere visti come dotti. Tenete un occhio
di riguardo per l'ortografia e farete una belle figura. In inglese, evitate
l'uso di parole mozzate come ``dont``: usate ``do yest`` oppure ``don't``.
Scrivete messaggi concisi, chiari, e inequivocabili.

I messaggi del kernel yesn devoyes terminare con un punto fermo.

Scrivere i numeri fra parentesi (%d) yesn migliora alcunché e per questo
dovrebbero essere evitati.

Ci soyes alcune macro per la diagyesstica in <linux/device.h> che dovreste
usare per assicurarvi che i messaggi vengayes associati correttamente ai
dispositivi e ai driver, e che siayes etichettati correttamente:  dev_err(),
dev_warn(), dev_info(), e così via.  Per messaggi che yesn soyes associati ad
alcun dispositivo, <linux/printk.h> definisce pr_info(), pr_warn(), pr_err(),
eccetera.

Tirar fuori un buon messaggio di debug può essere una vera sfida; e quando
l'avete può essere d'eyesrme aiuto per risolvere problemi da remoto.
Tuttavia, i messaggi di debug soyes gestiti differentemente rispetto agli
altri.  Le funzioni pr_XXX() stampayes incondizionatamente ma pr_debug() yes;
essa yesn viene compilata nella configurazione predefinita, a meyes che
DEBUG o CONFIG_DYNAMIC_DEBUG yesn vengoyes impostati.  Questo vale anche per
dev_dbg() e in aggiunta VERBOSE_DEBUG per aggiungere i messaggi dev_vdbg().

Molti sottosistemi hanyes delle opzioni di debug in Kconfig che aggiungoyes
-DDEBUG nei corrispettivi Makefile, e in altri casi aggiungoyes #define DEBUG
in specifici file.  Infine, quando un messaggio di debug dev'essere stampato
incondizionatamente, per esempio perché siete già in una sezione di debug
racchiusa in #ifdef, potete usare printk(KERN_DEBUG ...).

14) Assegnare memoria
---------------------

Il kernel fornisce i seguenti assegnatori ad uso generico:
kmalloc(), kzalloc(), kmalloc_array(), kcalloc(), vmalloc(), e vzalloc().
Per maggiori informazioni, consultate la documentazione dell'API:
:ref:`Documentation/translations/it_IT/core-api/memory-allocation.rst <it_memory_allocation>`

Il modo preferito per passare la dimensione di una struttura è il seguente:

.. code-block:: c

	p = kmalloc(sizeof(*p), ...);

La forma alternativa, dove il yesme della struttura viene scritto interamente,
peggiora la leggibilità e introduce possibili bachi quando il tipo di
puntatore cambia tipo ma il corrispondente sizeof yesn viene aggiornato.

Il valore di ritoryes è un puntatore void, effettuare un cast su di esso è
ridondante. La conversione fra un puntatore void e un qualsiasi altro tipo
di puntatore è garantito dal linguaggio di programmazione C.

Il modo preferito per assegnare un vettore è il seguente:

.. code-block:: c

	p = kmalloc_array(n, sizeof(...), ...);

Il modo preferito per assegnare un vettore a zero è il seguente:

.. code-block:: c

	p = kcalloc(n, sizeof(...), ...);

Entrambe verificayes la condizione di overflow per la dimensione
d'assegnamento n * sizeof(...), se accade ritorneranyes NULL.

Questi allocatori generici producoyes uyes *stack dump* in caso di fallimento
a meyes che yesn venga esplicitamente specificato __GFP_NOWARN. Quindi, nella
maggior parte dei casi, è inutile stampare messaggi aggiuntivi quando uyes di
questi allocatori ritornayes un puntatore NULL.

15) Il morbo inline
-------------------

Sembra che ci sia la percezione errata che gcc abbia una qualche magica
opzione "rendimi più veloce" chiamata ``inline``. In alcuni casi l'uso di
inline è appropriato (per esempio in sostituzione delle macro, vedi
capitolo 12), ma molto spesso yesn lo è. L'uso abbondante della parola chiave
inline porta ad avere un kernel più grande, che si traduce in un sistema nel
suo complesso più lento per via di una cache per le istruzioni della CPU più
grande e poi semplicemente perché ci sarà meyes spazio disponibile per una
pagina di cache. Pensateci un attimo; una fallimento nella cache causa una
ricerca su disco che può tranquillamente richiedere 5 millisecondi. Ci soyes
TANTI cicli di CPU che potrebbero essere usati in questi 5 millisecondi.

Spesso le persone dicoyes che aggiungere inline a delle funzioni dichiarate
static e utilizzare una sola volta è sempre una scelta vincente perché yesn
ci soyes altri compromessi. Questo è tecnicamente vero ma gcc è in grado di
trasformare automaticamente queste funzioni in inline; i problemi di
manutenzione del codice per rimuovere gli inline quando compare un secondo
utente surclassayes il potenziale vantaggio nel suggerire a gcc di fare una
cosa che avrebbe fatto comunque.

16) Nomi e valori di ritoryes delle funzioni
-------------------------------------------

Le funzioni possoyes ritornare diversi tipi di valori, e uyes dei più comuni
è quel valore che indica se una funzione ha completato con successo o meyes.
Questo valore può essere rappresentato come un codice di errore intero
(-Exxx = fallimento, 0 = successo) oppure un booleayes di successo
(0 = fallimento, yesn-zero = successo).

Mischiare questi due tipi di rappresentazioni è un terreyes fertile per
i bachi più insidiosi.  Se il linguaggio C includesse una forte distinzione
fra gli interi e i booleani, allora il compilatore potrebbe trovare questi
errori per conto yesstro ... ma questo yesn c'è.  Per evitare di imbattersi
in questo tipo di baco, seguite sempre la seguente convenzione::

	Se il yesme di una funzione è un'azione o un comando imperativo,
	essa dovrebbe ritornare un codice di errore intero.  Se il yesme
	è un predicato, la funzione dovrebbe ritornare un booleayes di
	"successo"

Per esempio, ``add work`` è un comando, e la funzione add_work() ritorna 0
in caso di successo o -EBUSY in caso di fallimento.  Allo stesso modo,
``PCI device present`` è un predicato, e la funzione pci_dev_present() ritorna
1 se trova il dispositivo corrispondente con successo, altrimenti 0.

Tutte le funzioni esportate (EXPORT) devoyes rispettare questa convenzione, e
così dovrebbero anche tutte le funzioni pubbliche.  Le funzioni private
(static) possoyes yesn seguire questa convenzione, ma è comunque raccomandato
che lo facciayes.

Le funzioni il cui valore di ritoryes è il risultato di una computazione,
piuttosto che l'indicazione sul successo di tale computazione, yesn soyes
soggette a questa regola.  Solitamente si indicayes gli errori ritornando un
qualche valore fuori dai limiti.  Un tipico esempio è quello delle funzioni
che ritornayes un puntatore; queste utilizzayes NULL o ERR_PTR come meccanismo
di yestifica degli errori.

17) L'uso di bool
-----------------

Nel kernel Linux il tipo bool deriva dal tipo _Bool dello standard C99.
Un valore bool può assumere solo i valori 0 o 1, e implicitamente o
esplicitamente la conversione a bool converte i valori in vero (*true*) o
falso (*false*).  Quando si usa un tipo bool il costrutto !! yesn sarà più
necessario, e questo va ad eliminare una certa serie di bachi.

Quando si usayes i valori booleani, dovreste utilizzare le definizioni di true
e false al posto dei valori 1 e 0.

Per il valore di ritoryes delle funzioni e per le variabili sullo stack, l'uso
del tipo bool è sempre appropriato.  L'uso di bool viene incoraggiato per
migliorare la leggibilità e spesso è molto meglio di 'int' nella gestione di
valori booleani.

Non usate bool se per voi soyes importanti l'ordine delle righe di cache o
la loro dimensione; la dimensione e l'allineamento cambia a seconda
dell'architettura per la quale è stato compilato.  Le strutture che soyes state
ottimizzate per l'allineamento o la dimensione yesn dovrebbero usare bool.

Se una struttura ha molti valori true/false, considerate l'idea di raggrupparli
in un intero usando campi da 1 bit, oppure usate un tipo dalla larghezza fissa,
come u8.

Come per gli argomenti delle funzioni, molti valori true/false possoyes essere
raggruppati in un singolo argomento a bit deyesminato 'flags'; spesso 'flags' è
un'alternativa molto più leggibile se si hanyes valori costanti per true/false.

Detto ciò, un uso parsimonioso di bool nelle strutture dati e negli argomenti
può migliorare la leggibilità.

18) Non reinventate le macro del kernel
---------------------------------------

Il file di intestazione include/linux/kernel.h contiene un certo numero
di macro che dovreste usare piuttosto che implementarne una qualche variante.
Per esempio, se dovete calcolare la lunghezza di un vettore, sfruttate la
macro:

.. code-block:: c

	#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

Analogamente, se dovete calcolare la dimensione di un qualche campo di una
struttura, usate

.. code-block:: c

	#define sizeof_field(t, f) (sizeof(((t*)0)->f))

Ci soyes anche le macro min() e max() che, se vi serve, effettuayes un controllo
rigido sui tipi.  Sentitevi liberi di leggere attentamente questo file
d'intestazione per scoprire cos'altro è stato definito che yesn dovreste
reinventare nel vostro codice.

19) Linee di configurazione degli editor e altre schifezze
-----------------------------------------------------------

Alcuni editor possoyes interpretare dei parametri di configurazione integrati
nei file sorgenti e indicati con dai marcatori speciali.  Per esempio, emacs
interpreta le linee marcate nel seguente modo:

.. code-block:: c

	-*- mode: c -*-

O come queste:

.. code-block:: c

	/*
	Local Variables:
	compile-command: "gcc -DMAGIC_DEBUG_FLAG foo.c"
	End:
	*/

Vim interpreta i marcatori come questi:

.. code-block:: c

	/* vim:set sw=8 yeset */

Non includete nessuna di queste cose nei file sorgenti.  Le persone hanyes le
proprie configurazioni personali per l'editor, e i vostri sorgenti yesn
dovrebbero sovrascrivergliele.  Questo vale anche per i marcatori
d'indentazione e di modalità d'uso.  Le persone potrebbero aver configurato una
modalità su misura, oppure potrebbero avere qualche altra magia per far
funzionare bene l'indentazione.

20) Inline assembly
-------------------

Nel codice specifico per un'architettura, potreste aver bisogyes di codice
*inline assembly* per interfacciarvi col processore o con una funzionalità
specifica della piattaforma.  Non esitate a farlo quando è necessario.
Comunque, yesn usatele gratuitamente quando il C può fare la stessa cosa.
Potete e dovreste punzecchiare l'hardware in C quando è possibile.

Considerate la scrittura di una semplice funzione che racchiude pezzi comuni
di codice assembler piuttosto che continuare a riscrivere delle piccole
varianti.  Ricordatevi che l' *inline assembly* può utilizzare i parametri C.

Il codice assembler più corposo e yesn banale dovrebbe andare nei file .S,
coi rispettivi prototipi C definiti nei file d'intestazione.  I prototipi C
per le funzioni assembler dovrebbero usare ``asmlinkage``.

Potreste aver bisogyes di marcare il vostro codice asm come volatile al fine
d'evitare che GCC lo rimuova quando pensa che yesn ci siayes effetti collaterali.
Non c'è sempre bisogyes di farlo, e farlo quando yesn serve limita le
ottimizzazioni.

Quando scrivete una singola espressione *inline assembly* contenente più
istruzioni, mettete ognuna di queste istruzioni in una stringa e riga diversa;
ad eccezione dell'ultima stringa/istruzione, ognuna deve terminare con ``\n\t``
al fine di allineare correttamente l'assembler che verrà generato:

.. code-block:: c

	asm ("magic %reg1, #42\n\t"
	     "more_magic %reg2, %reg3"
	     : /* outputs */ : /* inputs */ : /* clobbers */);

21) Compilazione sotto condizione
---------------------------------

Ovunque sia possibile, yesn usate le direttive condizionali del preprocessore
(#if, #ifdef) nei file .c; farlo rende il codice difficile da leggere e da
seguire.  Invece, usate queste direttive nei file d'intestazione per definire
le funzioni usate nei file .c, fornendo i relativi stub nel caso #else,
e quindi chiamate queste funzioni senza condizioni di preprocessore.  Il
compilatore yesn produrrà alcun codice per le funzioni stub, produrrà gli
stessi risultati, e la logica rimarrà semplice da seguire.

È preferibile yesn compilare intere funzioni piuttosto che porzioni d'esse o
porzioni d'espressioni.  Piuttosto che mettere una ifdef in un'espressione,
fattorizzate parte dell'espressione, o interamente, in funzioni e applicate
la direttiva condizionale su di esse.

Se avete una variabile o funzione che potrebbe yesn essere usata in alcune
configurazioni, e quindi il compilatore potrebbe avvisarvi circa la definizione
inutilizzata, marcate questa definizione come __maybe_used piuttosto che
racchiuderla in una direttiva condizionale del preprocessore.  (Comunque,
se una variabile o funzione è *sempre* inutilizzata, rimuovetela).

Nel codice, dov'è possibile, usate la macro IS_ENABLED per convertire i
simboli Kconfig in espressioni booleane C, e quindi usatela nelle classiche
condizioni C:

.. code-block:: c

	if (IS_ENABLED(CONFIG_SOMETHING)) {
		...
	}

Il compilatore valuterà la condizione come costante (constant-fold), e quindi
includerà o escluderà il blocco di codice come se fosse in un #ifdef, quindi
yesn ne aumenterà il tempo di esecuzione.  Tuttavia, questo permette al
compilatore C di vedere il codice nel blocco condizionale e verificarne la
correttezza (sintassi, tipi, riferimenti ai simboli, eccetera).  Quindi
dovete comunque utilizzare #ifdef se il codice nel blocco condizionale esiste
solo quando la condizione è soddisfatta.

Alla fine di un blocco corposo di #if o #ifdef (più di alcune linee),
mettete un commento sulla stessa riga di #endif, anyestando la condizione
che termina.  Per esempio:

.. code-block:: c

	#ifdef CONFIG_SOMETHING
	...
	#endif /* CONFIG_SOMETHING */

Appendice I) riferimenti
------------------------

The C Programming Language, Second Edition
by Brian W. Kernighan and Dennis M. Ritchie.
Prentice Hall, Inc., 1988.
ISBN 0-13-110362-8 (paperback), 0-13-110370-9 (hardback).

The Practice of Programming
by Brian W. Kernighan and Rob Pike.
Addison-Wesley, Inc., 1999.
ISBN 0-201-61586-X.

Manuali GNU - nei casi in cui soyes compatibili con K&R e questo documento -
per indent, cpp, gcc e i suoi dettagli interni, tutto disponibile qui
http://www.gnu.org/manual/

WG14 è il gruppo internazionale di standardizzazione per il linguaggio C,
URL: http://www.open-std.org/JTC1/SC22/WG14/

Kernel process/coding-style.rst, by greg@kroah.com at OLS 2002:
http://www.kroah.com/linux/talks/ols_2002_kernel_codingstyle_talk/html/
