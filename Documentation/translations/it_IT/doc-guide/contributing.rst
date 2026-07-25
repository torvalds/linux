.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-ita.rst

Come contribuire al miglioramento della documentazione del kernel
=================================================================

La documentazione è una parte importante di ogni progetto di sviluppo
software. Una buona documentazione aiuta ad attirare nuovi sviluppatori e
permette a quelli già presenti di lavorare in modo più efficace. Senza una
documentazione di qualità, si spreca molto tempo nel decifrare il codice a
ritroso e si commettono errori altrimenti evitabili.

Sfortunatamente, al momento la documentazione del kernel è ben lontana da
quello che dovrebbe essere per sostenere un progetto di queste dimensioni e
importanza.

Questa guida è per chi vuole contribuire a migliorare questa situazione. I
miglioramenti alla documentazione del kernel possono essere fatti da
sviluppatori con diversi livelli di esperienza; sono un modo relativamente
semplice per imparare il processo di sviluppo del kernel in generale e
trovare il proprio posto nella comunità. Quello che segue è, per la maggior
parte, l'elenco dei compiti che il manutentore della documentazione ritiene
più urgenti.

Le cose da fare nella documentazione
------------------------------------

C'è un elenco infinito di compiti da svolgere per portare la nostra
documentazione al livello in cui dovrebbe essere. Questo elenco contiene
alcuni punti importanti, ma è lungi dall'essere esaustivo; se trovate un
modo diverso per migliorare la documentazione, non esitate!

Correzione degli avvisi
~~~~~~~~~~~~~~~~~~~~~~~

Al momento, la generazione della documentazione produce un numero
incredibile di avvisi. Quando ce ne sono così tanti, è come se non ce ne
fosse nessuno: le persone li ignorano e non si accorgeranno mai quando il
loro lavoro ne aggiunge di nuovi. Per questo motivo, eliminare gli avvisi è
uno dei compiti a più alta priorità nell'elenco delle cose da fare per la
documentazione. Il compito in sé è ragionevolmente semplice, ma va
affrontato nel modo giusto per avere successo.

Gli avvisi emessi da un compilatore per il codice C possono spesso essere
scartati come falsi positivi, portando a patch il cui unico scopo è zittire
il compilatore. Gli avvisi generati dalla documentazione, invece, indicano
quasi sempre un problema reale; farli sparire richiede di comprendere il
problema e correggerlo alla radice. Per questo motivo, le patch che
correggono avvisi nella documentazione non dovrebbero limitarsi a dire "fix
a warning" nel titolo del changelog; dovrebbero invece indicare il problema
reale che è stato corretto.

Un altro punto importante è che gli avvisi nella documentazione sono spesso
generati da problemi nei commenti kerneldoc all'interno del codice C. Anche se
il manutentore della documentazione apprezza l'essere messo in copia sulle
correzioni di questo tipo, in realtà spesso rivolgersi al sottosistema di
documentazione non è il modo migliore di apportare queste modifiche; queste
dovrebbero invece essere inviate al manutentore del sottosistema in questione.

Per esempio, in una generazione della documentazione ho preso, quasi a
caso, un paio di avvisi::

  ./drivers/devfreq/devfreq.c:1818: warning: bad line:
  	- Resource-managed devfreq_register_notifier()
  ./drivers/devfreq/devfreq.c:1854: warning: bad line:
	- Resource-managed devfreq_unregister_notifier()

(Le righe sono state divise per essere più leggibili).

Una rapida occhiata al file sorgente indicato sopra ha rivelato un paio di
commenti kerneldoc con questo aspetto::

  /**
   * devm_devfreq_register_notifier()
	  - Resource-managed devfreq_register_notifier()
   * @dev:	The devfreq user device. (parent of devfreq)
   * @devfreq:	The devfreq object.
   * @nb:		The notifier block to be unregistered.
   * @list:	DEVFREQ_TRANSITION_NOTIFIER.
   */

Il problema è l'asterisco mancante, che confonde l'idea semplicistica che il
sistema di generazione abbia idea di come debba essere fatto un blocco di
commento C. Questo problema era presente fin da quando quel commento venne
aggiunto nel 2016, quindi da diversi anni. Correggerlo è stata solo questione di
aggiungere gli asterischi mancanti. Una rapida occhiata alla cronologia di quel
file ha mostrato quale fosse il formato usuale per la riga dell'oggetto, e
``scripts/get_maintainer.pl`` mi ha detto chi dovesse riceverla (basta passare
il percorso delle vostre patch come argomento a scripts/get_maintainer.pl). La
patch risultante era questa::

  [PATCH] PM / devfreq: Fix two malformed kerneldoc comments

  Two kerneldoc comments in devfreq.c fail to adhere to the required format,
  resulting in these doc-build warnings:

    ./drivers/devfreq/devfreq.c:1818: warning: bad line:
  	  - Resource-managed devfreq_register_notifier()
    ./drivers/devfreq/devfreq.c:1854: warning: bad line:
	  - Resource-managed devfreq_unregister_notifier()

  Add a couple of missing asterisks and make kerneldoc a little happier.

  Signed-off-by: Jonathan Corbet <corbet@lwn.net>
  ---
   drivers/devfreq/devfreq.c | 4 ++--
   1 file changed, 2 insertions(+), 2 deletions(-)

  diff --git a/drivers/devfreq/devfreq.c b/drivers/devfreq/devfreq.c
  index 57f6944d65a6..00c9b80b3d33 100644
  --- a/drivers/devfreq/devfreq.c
  +++ b/drivers/devfreq/devfreq.c
  @@ -1814,7 +1814,7 @@ static void devm_devfreq_notifier_release(struct device *dev, void *res)

   /**
    * devm_devfreq_register_notifier()
  -	- Resource-managed devfreq_register_notifier()
  + *	- Resource-managed devfreq_register_notifier()
    * @dev:	The devfreq user device. (parent of devfreq)
    * @devfreq:	The devfreq object.
    * @nb:		The notifier block to be unregistered.
  @@ -1850,7 +1850,7 @@ EXPORT_SYMBOL(devm_devfreq_register_notifier);

   /**
    * devm_devfreq_unregister_notifier()
  -	- Resource-managed devfreq_unregister_notifier()
  + *	- Resource-managed devfreq_unregister_notifier()
    * @dev:	The devfreq user device. (parent of devfreq)
    * @devfreq:	The devfreq object.
    * @nb:		The notifier block to be unregistered.
  --
  2.24.1

L'intero procedimento ha richiesto solo pochi minuti. Naturalmente, ho poi
scoperto che qualcun altro l'aveva già corretto in un altro albero,
mettendo in luce un'altra lezione: controllate sempre linux-next per
vedere se un problema è già stato risolto prima di mettervici sopra.

Altre correzioni richiederanno più tempo, specialmente quelle relative ai
campi di una struttura o ai parametri di una funzione privi di
documentazione. In questi casi, è necessario capire quale sia il ruolo di
questi campi o parametri e descriverli correttamente. Nel complesso, questo
compito diventa un po' tedioso a volte, ma è molto importante. Se riusciamo
davvero ad eliminare gli avvisi dalla generazione della documentazione,
allora potremo iniziare a pretendere che gli sviluppatori evitino di
aggiungerne di nuovi.

Oltre ai normali avvisi durante la generazione della documentazione, potete
ottenerne di più eseguendo ``make refcheckdocs`` per trovare riferimenti a file
di documentazione inesistenti.

Commenti kerneldoc dimenticati
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Gli sviluppatori sono incoraggiati a scrivere commenti kerneldoc per il
loro codice, ma molti di questi commenti non vengono mai inclusi nella
generazione della documentazione. Questo rende tale informazione più
difficile da trovare e, per esempio, impedisce a Sphinx di generare
collegamenti verso quella documentazione. Aggiungere le direttive
``kernel-doc`` alla documentazione per includere quei commenti può aiutare
la comunità a ottenere il pieno valore del lavoro speso per crearli.

Lo strumento ``tools/docs/find-unused-docs.sh`` può essere usato per
trovare questi commenti dimenticati.

Da notare che il valore maggiore deriva dall'includere la documentazione
per le funzioni e le strutture dati esportate. Molti sottosistemi hanno
anche commenti kerneldoc per uso interno; questi non dovrebbero essere
inclusi nella generazione della documentazione a meno che non vengano
posti in un documento specificamente rivolto agli sviluppatori che
lavorano all'interno del sottosistema in questione.


Correzione dei refusi
~~~~~~~~~~~~~~~~~~~~~

Correggere errori di battitura o di formattazione nella documentazione è
un modo rapido per imparare come creare e inviare patch, ed è un servizio
utile. Sono sempre disposto ad accettare questo tipo di patch. Detto
questo, una volta che ne avete corretti alcuni, considerate di passare a
compiti più avanzati, lasciando qualche refuso per il prossimo principiante
che vorrà occuparsene.

Da notare che alcune cose *non* sono refusi e non dovrebbero essere
"corrette":

 - Sia la grafia americana che quella britannica dell'inglese sono
   ammesse nella documentazione del kernel. Non c'è bisogno di sostituire
   l'una con l'altra.

 - La questione se un punto debba essere seguito da uno o due spazi non
   va dibattuta nel contesto della documentazione del kernel. Anche
   altri argomenti di legittimo disaccordo, come la "virgola di Oxford",
   non sono pertinenti qui.

Come per qualsiasi patch a qualsiasi progetto, considerate se la vostra
modifica sta davvero migliorando le cose.

Documentazione datata
~~~~~~~~~~~~~~~~~~~~~

Parte della documentazione del kernel è attuale, mantenuta e utile.
Un'altra parte... non lo è. Documentazione impolverata, vecchia e
imprecisa può fuorviare i lettori e gettare discredito sulla nostra
documentazione nel suo complesso. Qualsiasi cosa si possa fare per
affrontare questi problemi è più che benvenuta.

Ogni volta che lavorate su un documento, considerate se è attuale, se ha
bisogno di essere aggiornato, o se forse dovrebbe essere rimosso del
tutto. Ci sono alcuni segnali d'allarme a cui potete prestare attenzione:

 - Riferimenti a kernel della serie 2.x
 - Rimandi a repositori su SourceForge
 - Nella cronologia, negli ultimi anni, solo correzioni di refusi
 - Discussioni su modi di lavorare precedenti a Git

La cosa migliore da fare, ovviamente, sarebbe portare la documentazione a
essere attuale, aggiungendo qualsiasi informazione necessaria. Un lavoro
simile spesso richiede la collaborazione di sviluppatori che conoscono bene
il sottosistema in questione. Gli sviluppatori, quando viene chiesto loro
gentilmente, e quando le loro risposte vengono ascoltate e messe in
pratica, sono spesso più che disposti a collaborare con chi lavora per
migliorare la documentazione.

Alcuni documenti sono senza speranza; a volte troviamo documenti che fanno
riferimento a codice rimosso dal kernel molto tempo fa, per esempio. C'è
una sorprendente resistenza a rimuovere la documentazione obsoleta, ma
dovremmo farlo comunque. Il materiale superfluo nella nostra documentazione
non è d'aiuto a nessuno.

Nei casi in cui, forse, ci sono informazioni utili in un documento
gravemente datato, e non siete in grado di aggiornarlo, la cosa migliore
da fare potrebbe essere aggiungere un avviso all'inizio. Si raccomanda il
seguente testo::

  .. warning ::
  	This document is outdated and in need of attention.  Please use
	this information with caution, and please consider sending patches
	to update it.

In questo modo, almeno i nostri pazientissimi lettori sono stati avvisati
che il documento potrebbe portarli fuori strada.

Coerenza della documentazione
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

I veterani di qui ricorderanno i libri su Linux che comparvero sugli
scaffali negli anni '90. Erano semplicemente raccolte di file di
documentazione racimolati da varie fonti in rete. I libri sono (per lo
più) migliorati da allora, ma la documentazione del kernel è ancora per lo
più costruita su quel modello. Sono migliaia di file, quasi ognuno dei
quali è stato scritto in isolamento da tutti gli altri. Non abbiamo un
corpo coerente di documentazione del kernel; abbiamo migliaia di documenti
individuali.

Abbiamo cercato di migliorare la situazione creando un insieme di "libri"
che raggruppano la documentazione per specifici lettori. Questi
includono:

 - Documentation/admin-guide/index.rst
 - Documentation/core-api/index.rst
 - Documentation/driver-api/index.rst
 - Documentation/userspace-api/index.rst

Così come questo libro sulla documentazione stessa.

Spostare i documenti nei libri appropriati è un compito importante e deve
continuare. Ci sono, tuttavia, un paio di sfide associate a questo lavoro.
Spostare i file della documentazione, nel breve termine, infastidisce chi vi
lavora; comprensibilmente, non sono entusiasti di questi cambiamenti. Di solito
li si può convincere a spostarli una volta; tuttavia, non vogliamo continuare a
spostarli in giro.

Anche quando tutti i documenti sono al posto giusto, però, siamo solo
riusciti a trasformare un grande cumulo in un gruppo di cumuli più
piccoli. Il lavoro di cercare di tessere insieme tutti quei documenti in
un unico insieme non è ancora iniziato. Se avete idee brillanti su come
potremmo procedere su questo fronte, saremmo più che felici di sentirle.

Miglioramenti al foglio di stile
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Con l'adozione di Sphinx abbiamo un output HTML dall'aspetto molto più gradevole
di quanto avessimo un tempo. Ma è ancora migliorabile; Donald Knuth e Edward
Tufte non ne sarebbero impressionati. Questo richiede di modificare i nostri
fogli di stile per creare un output tipograficamente più solido, accessibile e
leggibile.

Attenzione: se vi assumete questo compito, vi state addentrando nel
classico territorio del "bikeshed". Aspettatevi molte opinioni e
discussioni anche per cambiamenti relativamente ovvi. Questa è, ahimè, la
natura del mondo in cui viviamo.

Generazione di PDF senza LaTeX
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Questo è un compito decisamente non banale per qualcuno con molto tempo a
disposizione e competenze in Python. La catena di strumenti di Sphinx è
relativamente piccola e ben contenuta; è facile da aggiungere a un sistema
di sviluppo. Ma generare output in PDF o EPUB richiede l'installazione di
LaTeX, che non è affatto piccolo o ben contenuto. Sarebbe una bella cosa
da eliminare.

La speranza originale era di usare lo strumento rst2pdf (https://rst2pdf.org/)
per la generazione dei PDF, ma si è scoperto che non era all'altezza del
compito. Il lavoro di sviluppo su rst2pdf sembra però essere ripreso di recente,
il che è un segno di speranza. Se uno sviluppatore adeguatamente motivato lo
migliorasse per far funzionare rst2pdf con la documentazione del kernel, il
mondo gli sarebbe eternamente grato.

Scrivere più documentazione
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Naturalmente, ci sono vaste parti del kernel che sono gravemente prive di
documentazione. Se avete la conoscenza per documentare uno specifico
sottosistema del kernel e il desiderio di farlo, non esitate a scrivere e
inviare il lavoro al kernel. Un numero incalcolabile di
sviluppatori e utenti del kernel vi ringrazierà.
