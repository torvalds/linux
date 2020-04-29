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

Per maggiori dettagli fate riferimento a :c:func:`array_size`,
:c:func:`array3_size`, e :c:func:`struct_size`, così come la famiglia di
funzioni :c:func:`check_add_overflow` e :c:func:`check_mul_overflow`.

simple_strtol(), simple_strtoll(), simple_strtoul(), simple_strtoull()
----------------------------------------------------------------------
Le funzioni :c:func:`simple_strtol`, :c:func:`simple_strtoll`,
:c:func:`simple_strtoul`, e :c:func:`simple_strtoull` ignorano volutamente
i possibili overflow, e questo può portare il chiamante a generare risultati
inaspettati. Le rispettive funzioni :c:func:`kstrtol`, :c:func:`kstrtoll`,
:c:func:`kstrtoul`, e :c:func:`kstrtoull` sono da considerarsi le corrette
sostitute; tuttavia va notato che queste richiedono che la stringa sia
terminata con il carattere NUL o quello di nuova riga.

strcpy()
--------
La funzione :c:func:`strcpy` non fa controlli agli estremi del buffer
di destinazione. Questo può portare ad un overflow oltre i limiti del
buffer e generare svariati tipi di malfunzionamenti. Nonostante l'opzione
`CONFIG_FORTIFY_SOURCE=y` e svariate opzioni del compilatore aiutano
a ridurne il rischio, non c'è alcuna buona ragione per continuare ad usare
questa funzione. La versione sicura da usare è :c:func:`strscpy`.

strncpy() su stringe terminate con NUL
--------------------------------------
L'utilizzo di :c:func:`strncpy` non fornisce alcuna garanzia sul fatto che
il buffer di destinazione verrà terminato con il carattere NUL. Questo
potrebbe portare a diversi overflow di lettura o altri malfunzionamenti
causati, appunto, dalla mancanza del terminatore. Questa estende la
terminazione nel buffer di destinazione quando la stringa d'origine è più
corta; questo potrebbe portare ad una penalizzazione delle prestazioni per
chi usa solo stringe terminate. La versione sicura da usare è
:c:func:`strscpy`. (chi usa :c:func:`strscpy` e necessita di estendere la
terminazione con NUL deve aggiungere una chiamata a :c:func:`memset`)

Se il chiamate no usa stringhe terminate con NUL, allore :c:func:`strncpy()`
può continuare ad essere usata, ma i buffer di destinazione devono essere
marchiati con l'attributo `__nonstring <https://gcc.gnu.org/onlinedocs/gcc/Common-Variable-Attributes.html>`_
per evitare avvisi durante la compilazione.

strlcpy()
---------
La funzione :c:func:`strlcpy`, per prima cosa, legge interamente il buffer di
origine, magari leggendo più di quanto verrà effettivamente copiato. Questo
è inefficiente e può portare a overflow di lettura quando la stringa non è
terminata con NUL. La versione sicura da usare è :c:func:`strscpy`.

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
