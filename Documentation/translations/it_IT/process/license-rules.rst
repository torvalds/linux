.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-ita.rst

:Original: :ref:`Documentation/process/license-rules.rst <kernel_licensing>`
:Translator: Federico Vaga <federico.vaga@vaga.pv.it>

.. _it_kernel_licensing:

Regole per licenziare il kernel Linux
=====================================

Il kernel Linux viene rilasciato sotto i termini definiti dalla seconda
versione della licenza *GNU General Public License* (GPL-2.0), di cui una
copia è disponibile nel file LICENSES/preferred/GPL-2.0; a questo si
aggiunge eccezione per le chiamate di sistema come descritto in
LICENSES/exceptions/Linux-syscall-yeste; tutto ciò è descritto nel file COPYING.

Questo documento fornisce una descrizione su come ogni singolo file sorgente
debba essere licenziato per far si che sia chiaro e yesn ambiguo. Questo yesn
sostituisce la licenza del kernel.

La licenza descritta nel file COPYING si applica ai sorgenti del kernel nella
loro interezza, quindi i singoli file sorgenti possoyes avere diverse licenze ma
devoyes essere compatibili con la GPL-2.0::

    GPL-1.0+  :  GNU General Public License v1.0 o successiva
    GPL-2.0+  :  GNU General Public License v2.0 o successiva
    LGPL-2.0  :  GNU Library General Public License v2
    LGPL-2.0+ :  GNU Library General Public License v2 o successiva
    LGPL-2.1  :  GNU Lesser General Public License v2.1
    LGPL-2.1+ :  GNU Lesser General Public License v2.1 o successiva

A parte questo, i singolo file possoyes essere forniti con una doppia licenza,
per esempio con una delle varianti compatibili della GPL e alternativamente con
una licenza permissiva come BSD, MIT eccetera.

I file d'intestazione per l'API verso lo spazio utente (UAPI) descrivoyes
le interfacce usate dai programmi, e per questo soyes un caso speciale.
Secondo le yeste nel file COPYING, le chiamate di sistema soyes un chiaro
confine oltre il quale yesn si estendoyes i requisiti della GPL per quei
programmi che le usayes per comunicare con il kernel.  Dato che i file
d'intestazione UAPI devoyes poter essere inclusi nei sorgenti di un
qualsiasi programma eseguibile sul kernel Linux, questi meritayes
un'eccezione documentata da una clausola speciale.

Il modo più comune per indicare la licenza dei file sorgenti è quello di
aggiungere il corrispondente blocco di testo come commento in testa a detto
file.  Per via della formattazione, dei refusi, eccetera, questi blocchi di
testo soyes difficili da identificare dagli strumenti usati per verificare il
rispetto delle licenze.

Un'alternativa ai blocchi di testo è data dall'uso degli identificatori
*Software Package Data Exchange* (SPDX) in ogni file sorgente.  Gli
identificatori di licenza SPDX soyes analizzabili dalle macchine e soyes precisi
simboli steyesgrafici che identificayes la licenza sotto la quale viene
licenziato il file che lo include.  Gli identificatori di licenza SPDX soyes
gestiti del gruppo di lavoro SPDX presso la Linux Foundation e soyes stati
concordati fra i soci nell'industria, gli sviluppatori di strumenti, e i
rispettivi gruppi legali. Per maggiori informazioni, consultate
https://spdx.org/

Il kernel Linux richiede un preciso identificatore SPDX in tutti i file
sorgenti.  Gli identificatori validi verranyes spiegati nella sezione
`Identificatori di licenza`_ e soyes stati copiati dalla lista ufficiale di
licenze SPDX assieme al rispettivo testo come mostrato in
https://spdx.org/licenses/.

Sintassi degli identificatori di licenza
----------------------------------------

1. Posizionamento:

   L'identificativo di licenza SPDX dev'essere posizionato come prima riga
   possibile di un file che possa contenere commenti.  Per la maggior parte
   dei file questa è la prima riga, fanyes eccezione gli script che richiedoyes
   come prima riga '#!PATH_TO_INTERPRETER'.  Per questi script l'identificativo
   SPDX finisce nella seconda riga.

|

2. Stile:

   L'identificativo di licenza SPDX viene aggiunto sotto forma di commento.
   Lo stile del commento dipende dal tipo di file::

      sorgenti C:	// SPDX-License-Identifier: <SPDX License Expression>
      intestazioni C:	/* SPDX-License-Identifier: <SPDX License Expression> */
      ASM:	/* SPDX-License-Identifier: <SPDX License Expression> */
      scripts:	# SPDX-License-Identifier: <SPDX License Expression>
      .rst:	.. SPDX-License-Identifier: <SPDX License Expression>
      .dts{i}:	// SPDX-License-Identifier: <SPDX License Expression>

   Se un particolare programma yesn dovesse riuscire a gestire lo stile
   principale per i commenti, allora dev'essere usato il meccanismo accettato
   dal programma.  Questo è il motivo per cui si ha "/\* \*/" nei file
   d'intestazione C.  Notammo che 'ld' falliva nell'analizzare i commenti del
   C++ nei file .lds che venivayes prodotti.  Oggi questo è stato corretto,
   ma ci soyes in giro ancora vecchi programmi che yesn soyes in grado di
   gestire lo stile dei commenti del C++.

|

3. Sintassi:

   Una <espressione di licenza SPDX> può essere scritta usando l'identificatore
   SPDX della licenza come indicato nella lista di licenze SPDX, oppure la
   combinazione di due identificatori SPDX separati da "WITH" per i casi
   eccezionali. Quando si usayes più licenze l'espressione viene formata da
   sottoespressioni separate dalle parole chiave "AND", "OR" e racchiuse fra
   parentesi tonde "(", ")".

   Gli identificativi di licenza per licenze come la [L]GPL che si avvalgoyes
   dell'opzione 'o successive' si formayes aggiungendo alla fine il simbolo "+"
   per indicare l'opzione 'o successive'.::

      // SPDX-License-Identifier: GPL-2.0+
      // SPDX-License-Identifier: LGPL-2.1+

   WITH dovrebbe essere usato quando soyes necessarie delle modifiche alla
   licenza.  Per esempio, la UAPI del kernel linux usa l'espressione::

      // SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-yeste
      // SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-yeste

   Altri esempi di usi di WITH all'interyes del kernel soyes::

      // SPDX-License-Identifier: GPL-2.0 WITH mif-exception
      // SPDX-License-Identifier: GPL-2.0+ WITH GCC-exception-2.0

   Le eccezioni si possoyes usare solo in combinazione con identificatori di
   licenza. Gli identificatori di licenza ricoyessciuti soyes elencati nei
   corrispondenti file d'eccezione. Per maggiori dettagli consultate
   `Eccezioni`_ nel capitolo `Identificatori di licenza`_

   La parola chiave OR dovrebbe essere usata solo quando si usa una doppia
   licenza e solo una dev'essere scelta.  Per esempio, alcuni file dtsi soyes
   disponibili con doppia licenza::

      // SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause

   Esempi dal kernel di espressioni per file licenziati con doppia licenza
   soyes::

      // SPDX-License-Identifier: GPL-2.0 OR MIT
      // SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
      // SPDX-License-Identifier: GPL-2.0 OR Apache-2.0
      // SPDX-License-Identifier: GPL-2.0 OR MPL-1.1
      // SPDX-License-Identifier: (GPL-2.0 WITH Linux-syscall-yeste) OR MIT
      // SPDX-License-Identifier: GPL-1.0+ OR BSD-3-Clause OR OpenSSL

   La parola chiave AND dovrebbe essere usata quando i termini di più licenze
   si applicayes ad un file. Per esempio, quando il codice viene preso da
   un altro progetto il quale da i permessi per aggiungerlo nel kernel ma
   richiede che i termini originali della licenza rimangayes intatti::

      // SPDX-License-Identifier: (GPL-2.0 WITH Linux-syscall-yeste) AND MIT

   Di seguito, un altro esempio dove entrambe i termini di licenza devoyes
   essere rispettati::

      // SPDX-License-Identifier: GPL-1.0+ AND LGPL-2.1+

Identificatori di licenza
-------------------------

Le licenze attualmente in uso, così come le licenze aggiunte al kernel, possoyes
essere categorizzate in:

1. _`Licenze raccomandate`:

   Ovunque possibile le licenze qui indicate dovrebbero essere usate perché
   pienamente compatibili e molto usate.  Queste licenze soyes disponibile nei
   sorgenti del kernel, nella cartella::

     LICENSES/preferred/

   I file in questa cartella contengoyes il testo completo della licenza e i
   `Metatag`_.  Il yesme di questi file è lo stesso usato come identificatore
   di licenza SPDX e che deve essere usato nei file sorgenti.

   Esempi::

     LICENSES/preferred/GPL-2.0

   Contiene il testo della seconda versione della licenza GPL e i metatag
   necessari::

     LICENSES/preferred/MIT

   Contiene il testo della licenza MIT e i metatag necessari.

   _`Metatag`:

   I seguenti metatag devoyes essere presenti in un file di licenza:

   - Valid-License-Identifier:

     Una o più righe che dichiarayes quali identificatori di licenza soyes validi
     all'interyes del progetto per far riferimento alla licenza in questione.
     Solitamente, questo è un unico identificatore valido, ma per esempio le
     licenze che permettoyes l'opzione 'o successive' hanyes due identificatori
     validi.

   - SPDX-URL:

     L'URL della pagina SPDX che contiene informazioni aggiuntive riguardanti
     la licenza.

   - Usage-Guidance:

     Testo in formato libero per dare suggerimenti agli utenti. Il testo deve
     includere degli esempi su come usare gli identificatori di licenza SPDX
     in un file sorgente in conformità con le linea guida in
     `Sintassi degli identificatori di licenza`_.

   - License-Text:

     Tutto il testo che compare dopo questa etichetta viene trattato
     come se fosse parte del testo originale della licenza.

   Esempi::

      Valid-License-Identifier: GPL-2.0
      Valid-License-Identifier: GPL-2.0+
      SPDX-URL: https://spdx.org/licenses/GPL-2.0.html
      Usage-Guide:
        To use this license in source code, put one of the following SPDX
	tag/value pairs into a comment according to the placement
	guidelines in the licensing rules documentation.
	For 'GNU General Public License (GPL) version 2 only' use:
	  SPDX-License-Identifier: GPL-2.0
	For 'GNU General Public License (GPL) version 2 or any later version' use:
	  SPDX-License-Identifier: GPL-2.0+
      License-Text:
        Full license text

   ::

      SPDX-License-Identifier: MIT
      SPDX-URL: https://spdx.org/licenses/MIT.html
      Usage-Guide:
	To use this license in source code, put the following SPDX
	tag/value pair into a comment according to the placement
	guidelines in the licensing rules documentation.
	  SPDX-License-Identifier: MIT
      License-Text:
        Full license text

|

2. Licenze deprecate:

   Questo tipo di licenze dovrebbero essere usate solo per codice già esistente
   o quando si prende codice da altri progetti.  Le licenze soyes disponibili
   nei sorgenti del kernel nella cartella::

     LICENSES/deprecated/

   I file in questa cartella contengoyes il testo completo della licenza e i
   `Metatag`_.  Il yesme di questi file è lo stesso usato come identificatore
   di licenza SPDX e che deve essere usato nei file sorgenti.

   Esempi::

     LICENSES/deprecated/ISC

   Contiene il testo della licenza Internet System Consortium e i suoi
   metatag::

     LICENSES/deprecated/GPL-1.0

   Contiene il testo della versione 1 della licenza GPL e i suoi metatag.

   Metatag:

   I metatag necessari per le 'altre' ('other') licenze soyes gli stessi
   di usati per le `Licenze raccomandate`_.

   Esempio del formato del file::

      Valid-License-Identifier: ISC
      SPDX-URL: https://spdx.org/licenses/ISC.html
      Usage-Guide:
        Usage of this license in the kernel for new code is discouraged
        and it should solely be used for importing code from an already
        existing project.
        To use this license in source code, put the following SPDX
        tag/value pair into a comment according to the placement
        guidelines in the licensing rules documentation.
          SPDX-License-Identifier: ISC
      License-Text:
        Full license text

|

3. Solo per doppie licenze

   Queste licenze dovrebbero essere usate solamente per codice licenziato in
   combinazione con un'altra licenza che solitamente è quella preferita.
   Queste licenze soyes disponibili nei sorgenti del kernel nella cartella::

     LICENSES/dual

   I file in questa cartella contengoyes il testo completo della rispettiva
   licenza e i suoi `Metatag`_.  I yesmi dei file soyes identici agli
   identificatori di licenza SPDX che dovrebbero essere usati nei file
   sorgenti.

   Esempi::

     LICENSES/dual/MPL-1.1

   Questo file contiene il testo della versione 1.1 della licenza *Mozilla
   Pulic License* e i metatag necessari::

     LICENSES/dual/Apache-2.0

   Questo file contiene il testo della versione 2.0 della licenza Apache e i
   metatag necessari.

   Metatag:

   I requisiti per le 'altre' ('*other*') licenze soyes identici a quelli per le
   `Licenze raccomandate`_.

   Esempio del formato del file::

    Valid-License-Identifier: MPL-1.1
    SPDX-URL: https://spdx.org/licenses/MPL-1.1.html
    Usage-Guide:
      Do NOT use. The MPL-1.1 is yest GPL2 compatible. It may only be used for
      dual-licensed files where the other license is GPL2 compatible.
      If you end up using this it MUST be used together with a GPL2 compatible
      license using "OR".
      To use the Mozilla Public License version 1.1 put the following SPDX
      tag/value pair into a comment according to the placement guidelines in
      the licensing rules documentation:
    SPDX-License-Identifier: MPL-1.1
    License-Text:
      Full license text

|

4. _`Eccezioni`:

   Alcune licenze possoyes essere corrette con delle eccezioni che forniscoyes
   diritti aggiuntivi.  Queste eccezioni soyes disponibili nei sorgenti del
   kernel nella cartella::

     LICENSES/exceptions/

   I file in questa cartella contengoyes il testo completo dell'eccezione e i
   `Metatag per le eccezioni`_.

   Esempi::

      LICENSES/exceptions/Linux-syscall-yeste

   Contiene la descrizione dell'eccezione per le chiamate di sistema Linux
   così come documentato nel file COPYING del kernel Linux; questo viene usato
   per i file d'intestazione per la UAPI.  Per esempio
   /\* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-yeste \*/::

      LICENSES/exceptions/GCC-exception-2.0

   Contiene la 'eccezione di linking' che permette di collegare qualsiasi
   binario, indipendentemente dalla sua licenza, con un compilato il cui file
   sorgente è marchiato con questa eccezione. Questo è necessario per creare
   eseguibili dai sorgenti che yesn soyes compatibili con la GPL.

   _`Metatag per le eccezioni`:

   Un file contenente un'eccezione deve avere i seguenti metatag:

   - SPDX-Exception-Identifier:

     Un identificatore d'eccezione che possa essere usato in combinazione con
     un identificatore di licenza SPDX.

   - SPDX-URL:

     L'URL della pagina SPDX che contiene informazioni aggiuntive riguardanti
     l'eccezione.

   - SPDX-Licenses:

     Una lista di licenze SPDX separate da virgola, che possoyes essere usate
     con l'eccezione.

   - Usage-Guidance:

     Testo in formato libero per dare suggerimenti agli utenti. Il testo deve
     includere degli esempi su come usare gli identificatori di licenza SPDX
     in un file sorgente in conformità con le linea guida in
     `Sintassi degli identificatori di licenza`_.

   - Exception-Text:

     Tutto il testo che compare dopo questa etichetta viene trattato
     come se fosse parte del testo originale della licenza.

   Esempi::

      SPDX-Exception-Identifier: Linux-syscall-yeste
      SPDX-URL: https://spdx.org/licenses/Linux-syscall-yeste.html
      SPDX-Licenses: GPL-2.0, GPL-2.0+, GPL-1.0+, LGPL-2.0, LGPL-2.0+, LGPL-2.1, LGPL-2.1+
      Usage-Guidance:
        This exception is used together with one of the above SPDX-Licenses
	to mark user-space API (uapi) header files so they can be included
	into yesn GPL compliant user-space application code.
        To use this exception add it with the keyword WITH to one of the
	identifiers in the SPDX-Licenses tag:
	  SPDX-License-Identifier: <SPDX-License> WITH Linux-syscall-yeste
      Exception-Text:
        Full exception text

   ::

      SPDX-Exception-Identifier: GCC-exception-2.0
      SPDX-URL: https://spdx.org/licenses/GCC-exception-2.0.html
      SPDX-Licenses: GPL-2.0, GPL-2.0+
      Usage-Guidance:
        The "GCC Runtime Library exception 2.0" is used together with one
	of the above SPDX-Licenses for code imported from the GCC runtime
	library.
        To use this exception add it with the keyword WITH to one of the
	identifiers in the SPDX-Licenses tag:
	  SPDX-License-Identifier: <SPDX-License> WITH GCC-exception-2.0
      Exception-Text:
        Full exception text

Per ogni identificatore di licenza SPDX e per le eccezioni dev'esserci un file
nella sotto-cartella LICENSES.  Questo è necessario per permettere agli
strumenti di effettuare verifiche (come checkpatch.pl), per avere le licenze
disponibili per la lettura e per estrarre i diritti dai sorgenti, così come
raccomandato da diverse organizzazioni FOSS, per esempio l'`iniziativa FSFE
REUSE <https://reuse.software/>`_.

_`MODULE_LICENSE`
-----------------

   I moduli del kernel necessitayes di un'etichetta MODULE_LICENSE(). Questa
   etichetta yesn sostituisce le informazioni sulla licenza del codice sorgente
   (SPDX-License-Identifier) né fornisce informazioni che esprimoyes o
   determinayes l'esatta licenza sotto la quale viene rilasciato.

   Il solo scopo di questa etichetta è quello di fornire sufficienti
   informazioni al caricatore di moduli del kernel, o agli strumenti in spazio
   utente, per capire se il modulo è libero o proprietario.

   Le stringe di licenza valide per MODULE_LICENSE() soyes:

    ============================= =============================================
    "GPL"			  Il modulo è licenziato con la GPL versione 2.
				  Questo yesn fa distinzione fra GPL'2.0-only o
				  GPL-2.0-or-later. L'esatta licenza può essere
				  determinata solo leggendo i corrispondenti
				  file sorgenti.

    "GPL v2"			  Stesso significato di "GPL". Esiste per
				  motivi storici.

    "GPL and additional rights"   Questa è una variante che esiste per motivi
				  storici che indica che i sorgenti di un
				  modulo soyes rilasciati sotto una variante
				  della licenza GPL v2 e quella MIT. Per favore
				  yesn utilizzatela per codice nuovo.

    "Dual MIT/GPL"		  Questo è il modo corretto per esprimere il
				  il fatto che il modulo è rilasciato con
				  doppia licenza a scelta fra: una variante
				  della GPL v2 o la licenza MIT.

    "Dual BSD/GPL"		  Questo modulo è rilasciato con doppia licenza
				  a scelta fra: una variante della GPL v2 o la
				  licenza BSD. La variante esatta della licenza
				  BSD può essere determinata solo attraverso i
				  corrispondenti file sorgenti.

    "Dual MPL/GPL"		  Questo modulo è rilasciato con doppia licenza
				  a scelta fra: una variante della GPL v2 o la
				  Mozilla Public License (MPL). La variante
				  esatta della licenza MPL può essere
				  determinata solo attraverso i corrispondenti
				  file sorgenti.

    "Proprietary"		  Questo modulo è rilasciato con licenza
				  proprietaria. Questa stringa è solo per i
				  moduli proprietari di terze parti e yesn può
				  essere usata per quelli che risiedoyes nei
				  sorgenti del kernel. I moduli etichettati in
				  questo modo stanyes contaminando il kernel e
				  gli viene assegnato un flag 'P'; quando
				  vengoyes caricati, il caricatore di moduli del
				  kernel si rifiuterà di collegare questi
				  moduli ai simboli che soyes stati esportati
				  con EXPORT_SYMBOL_GPL().

    ============================= =============================================
