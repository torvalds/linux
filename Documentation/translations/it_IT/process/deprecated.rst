.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-ita.rst

:Original: :ref:`Documentation/process/deprecated.rst <deprecated>`
:Translator: Federico Vaga <federico.vaga@vaga.pv.it>

.. _it_deprecated:

==============================================================================
Interfacce deprecate, caratteristiche del linguaggio, attributi, e convenzioni
==============================================================================

In un mondo perfetto, sarebbe possibile prendere tutti gli usi di
un'interfaccia deprecata e convertirli in quella nuova, e così sarebbe
possibile rimuovere la vecchia interfaccia in un singolo ciclo di sviluppo.
Tuttavia, per via delle dimensioni del kernel, la gerarchia dei manutentori e
le tempistiche, non è sempre possibile fare questo tipo di conversione tutta
in una volta. Questo significa che nuove istanze di una vecchia interfaccia
potrebbero aggiungersi al kernel proprio quando si sta cercando di rimuoverle,
aumentando così il carico di lavoro. Al fine di istruire gli sviluppatori su
cosa è considerato deprecato (e perché), è stata create la seguente lista a cui
fare riferimento quando qualcuno propone modifiche che usano cose deprecate.

__deprecated
------------
Nonostante questo attributo marchi visibilmente un interfaccia come deprecata,
`non produce più alcun avviso durante la compilazione
<https://git.kernel.org/linus/771c035372a036f83353eef46dbb829780330234>`_
perché uno degli obiettivi del kernel è quello di compilare senza avvisi;
inoltre, nessuno stava agendo per rimuovere queste interfacce. Nonostante l'uso
di `__deprecated` in un file d'intestazione sia opportuno per segnare una
interfaccia come 'vecchia', questa non è una soluzione completa. L'interfaccia
deve essere rimossa dal kernel, o aggiunta a questo documento per scoraggiarne
l'uso.

BUG() e BUG_ON()
----------------
Al loro posto usate WARN() e WARN_ON() per gestire le
condizioni "impossibili" e gestitele come se fosse possibile farlo.
Nonostante le funzioni della famiglia BUG() siano state progettate
per asserire "situazioni impossibili" e interrompere in sicurezza un
thread del kernel, queste si sono rivelate essere troppo rischiose
(per esempio, in quale ordine rilasciare i *lock*? Ci sono stati che
sono stati ripristinati?). Molto spesso l'uso di BUG()
destabilizza il sistema o lo corrompe del tutto, il che rende
impossibile un'attività di debug o anche solo leggere un rapporto
circa l'errore.  Linus ha un'opinione molto critica al riguardo:
`email 1
<https://lore.kernel.org/lkml/CA+55aFy6jNLsywVYdGp83AMrXBo_P-pkjkphPGrO=82SPKCpLQ@mail.gmail.com/>`_,
`email 2
<https://lore.kernel.org/lkml/CAHk-=whDHsbK3HTOpTF=ue_o04onRwTEaK_ZoJp_fjbqq4+=Jw@mail.gmail.com/>`_

Tenete presente che la famiglia di funzioni WARN() dovrebbe essere
usato solo per situazioni che si suppone siano "impossibili".  Se
volete avvisare gli utenti riguardo a qualcosa di possibile anche se
indesiderato, usare le funzioni della famiglia pr_warn().  Chi
amministra il sistema potrebbe aver attivato l'opzione sysctl
*panic_on_warn* per essere sicuri che il sistema smetta di funzionare
in caso si verifichino delle condizioni "inaspettate". (per esempio,
date un'occhiata al questo `commit
<https://git.kernel.org/linus/d4689846881d160a4d12a514e991a740bcb5d65a>`_)

Calcoli codificati negli argomenti di un allocatore
----------------------------------------------------
Il calcolo dinamico delle dimensioni (specialmente le moltiplicazioni) non
dovrebbero essere fatto negli argomenti di funzioni di allocazione di memoria
(o simili) per via del rischio di overflow. Questo può portare a valori più
piccoli di quelli che il chiamante si aspettava. L'uso di questo modo di
allocare può portare ad un overflow della memoria di heap e altri
malfunzionamenti. (Si fa eccezione per valori numerici per i quali il
compilatore può generare avvisi circa un potenziale overflow. Tuttavia usare
i valori numerici come suggerito di seguito è innocuo).

Per esempio, non usate ``count * size`` come argomento::

	foo = kmalloc(count * size, GFP_KERNEL);

Al suo posto, si dovrebbe usare l'allocatore a due argomenti::

	foo = kmalloc_array(count, size, GFP_KERNEL);

Se questo tipo di allocatore non è disponibile, allora dovrebbero essere usate
le funzioni del tipo *saturate-on-overflow*::

	bar = vmalloc(array_size(count, size));

Un altro tipico caso da evitare è quello di calcolare la dimensione di una
struttura seguita da un vettore di altre strutture, come nel seguente caso::

	header = kzalloc(sizeof(*header) + count * sizeof(*header->item),
			 GFP_KERNEL);

Invece, usate la seguente funzione::

	header = kzalloc(struct_size(header, item, count), GFP_KERNEL);

Per maggiori dettagli fate riferimento a array_size(),
array3_size(), e struct_size(), così come la famiglia di
funzioni check_add_overflow() e check_mul_overflow().

simple_strtol(), simple_strtoll(), simple_strtoul(), simple_strtoull()
----------------------------------------------------------------------
Le funzioni simple_strtol(), simple_strtoll(),
simple_strtoul(), e simple_strtoull() ignorano volutamente
i possibili overflow, e questo può portare il chiamante a generare risultati
inaspettati. Le rispettive funzioni kstrtol(), kstrtoll(),
kstrtoul(), e kstrtoull() sono da considerarsi le corrette
sostitute; tuttavia va notato che queste richiedono che la stringa sia
terminata con il carattere NUL o quello di nuova riga.

strcpy()
--------
La funzione strcpy() non fa controlli agli estremi del buffer
di destinazione. Questo può portare ad un overflow oltre i limiti del
buffer e generare svariati tipi di malfunzionamenti. Nonostante l'opzione
`CONFIG_FORTIFY_SOURCE=y` e svariate opzioni del compilatore aiutano
a ridurne il rischio, non c'è alcuna buona ragione per continuare ad usare
questa funzione. La versione sicura da usare è strscpy().

strncpy() su stringe terminate con NUL
--------------------------------------
L'utilizzo di strncpy() non fornisce alcuna garanzia sul fatto che
il buffer di destinazione verrà terminato con il carattere NUL. Questo
potrebbe portare a diversi overflow di lettura o altri malfunzionamenti
causati, appunto, dalla mancanza del terminatore. Questa estende la
terminazione nel buffer di destinazione quando la stringa d'origine è più
corta; questo potrebbe portare ad una penalizzazione delle prestazioni per
chi usa solo stringe terminate. La versione sicura da usare è
strscpy(). (chi usa strscpy() e necessita di estendere la
terminazione con NUL deve aggiungere una chiamata a memset())

Se il chiamate no usa stringhe terminate con NUL, allore strncpy()
può continuare ad essere usata, ma i buffer di destinazione devono essere
marchiati con l'attributo `__nonstring <https://gcc.gnu.org/onlinedocs/gcc/Common-Variable-Attributes.html>`_
per evitare avvisi durante la compilazione.

strlcpy()
---------
La funzione strlcpy(), per prima cosa, legge interamente il buffer di
origine, magari leggendo più di quanto verrà effettivamente copiato. Questo
è inefficiente e può portare a overflow di lettura quando la stringa non è
terminata con NUL. La versione sicura da usare è strscpy().

Segnaposto %p nella stringa di formato
--------------------------------------

Tradizionalmente, l'uso del segnaposto "%p" nella stringa di formato
esponne un indirizzo di memoria in dmesg, proc, sysfs, eccetera.  Per
evitare che questi indirizzi vengano sfruttati da malintenzionati,
tutto gli usi di "%p" nel kernel rappresentano l'hash dell'indirizzo,
rendendolo di fatto inutilizzabile.  Nuovi usi di "%p" non dovrebbero
essere aggiunti al kernel.  Per una rappresentazione testuale di un
indirizzo usate "%pS", l'output è migliore perché mostrerà il nome del
simbolo.  Per tutto il resto, semplicemente non usate "%p".

Parafrasando la `guida
<https://lore.kernel.org/lkml/CA+55aFwQEd_d40g4mUCSsVRZzrFPUJt74vc6PPpb675hYNXcKw@mail.gmail.com/>`_
di Linus:

- Se il valore hash di "%p" è inutile, chiediti se il puntatore stesso
  è importante. Forse dovrebbe essere rimosso del tutto?
- Se credi davvero che il vero valore del puntatore sia importante,
  perché alcuni stati del sistema o i livelli di privilegi di un
  utente sono considerati "special"? Se pensi di poterlo giustificare
  (in un commento e nel messaggio del commit) abbastanza bene da
  affrontare il giudizio di Linus, allora forse potrai usare "%px",
  assicurandosi anche di averne il permesso.

Infine, sappi che un cambio in favore di "%p" con hash `non verrà
accettato
<https://lore.kernel.org/lkml/CA+55aFwieC1-nAs+NFq9RTwaR8ef9hWa4MjNBWL41F-8wM49eA@mail.gmail.com/>`_.

Vettori a dimensione variabile (VLA)
------------------------------------

Usare VLA sullo stack produce codice molto peggiore rispetto a quando si usano
vettori a dimensione fissa. Questi `problemi di prestazioni <https://git.kernel.org/linus/02361bc77888>`_,
tutt'altro che banali, sono già un motivo valido per eliminare i VLA; in
aggiunta sono anche un problema per la sicurezza. La crescita dinamica di un
vettore nello stack potrebbe eccedere la memoria rimanente in tale segmento.
Questo può portare a dei malfunzionamenti, potrebbe sovrascrivere
dati importanti alla fine dello stack (quando il kernel è compilato senza
`CONFIG_THREAD_INFO_IN_TASK=y`), o sovrascrivere un pezzo di memoria adiacente
allo stack (quando il kernel è compilato senza `CONFIG_VMAP_STACK=y`).

Salto implicito nell'istruzione switch-case
-------------------------------------------

Il linguaggio C permette ai casi di un'istruzione `switch` di saltare al
prossimo caso quando l'istruzione "break" viene omessa alla fine del caso
corrente. Tuttavia questo rende il codice ambiguo perché non è sempre ovvio se
l'istruzione "break" viene omessa intenzionalmente o è un baco. Per esempio,
osservando il seguente pezzo di codice non è chiaro se lo stato
`STATE_ONE` è stato progettato apposta per eseguire anche `STATE_TWO`::

  switch (value) {
  case STATE_ONE:
          do_something();
  case STATE_TWO:
          do_other();
          break;
  default:
          WARN("unknown state");
  }

Dato che c'è stata una lunga lista di problemi `dovuti alla mancanza dell'istruzione
"break" <https://cwe.mitre.org/data/definitions/484.html>`_, oggigiorno non
permettiamo più che vi sia un "salto implicito" (*fall-through*). Per
identificare un salto implicito intenzionale abbiamo adottato la pseudo
parola chiave 'fallthrough' che viene espansa nell'estensione di gcc
`__attribute__((fallthrough))` `Statement Attributes
<https://gcc.gnu.org/onlinedocs/gcc/Statement-Attributes.html>`_.
(Quando la sintassi C17/C18 `[[fallthrough]]` sarà più comunemente
supportata dai compilatori C, analizzatori statici, e dagli IDE,
allora potremo usare quella sintassi per la pseudo parola chiave)

Quando la sintassi [[fallthrough]] sarà più comunemente supportata dai
compilatori, analizzatori statici, e ambienti di sviluppo IDE,
allora potremo usarla anche noi.

Ne consegue che tutti i blocchi switch/case devono finire in uno dei seguenti
modi:

* ``break;``
* `fallthrough;``
* ``continue;``
* ``goto <label>;``
* ``return [expression];``
