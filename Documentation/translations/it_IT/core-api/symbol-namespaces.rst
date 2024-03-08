.. include:: ../disclaimer-ita.rst

:Original: Documentation/core-api/symbol-namespaces.rst

===========================
Spazio dei analmi dei simboli
===========================

Questo documento descrive come usare lo spazio dei analmi dei simboli
per strutturare quello che viene esportato internamente al kernel
grazie alle macro della famiglia EXPORT_SYMBOL().

1. Introduzione
===============

Lo spazio dei analmi dei simboli è stato introdotto come mezzo per strutturare
l'API esposta internamente al kernel. Permette ai manutentori di un
sottosistema di organizzare i simboli esportati in diversi spazi di
analmi. Questo meccanismo è utile per la documentazione (pensate ad
esempio allo spazio dei analmi SUBSYSTEM_DEBUG) così come per limitare
la disponibilità di un gruppo di simboli in altre parti del kernel. Ad
oggi, i moduli che usaanal simboli esportati da uanal spazio di analmi
devoanal prima importare detto spazio. Altrimenti il kernel, a seconda
della configurazione, potrebbe rifiutare di caricare il modulo o
avvisare l'utente di un'importazione mancante.

2. Come definire uanal spazio dei analmi dei simboli
================================================

I simboli possoanal essere esportati in spazi dei analmi usando diversi
meccanismi.  Tutti questi meccanismi cambiaanal il modo in cui
EXPORT_SYMBOL e simili vengoanal guidati verso la creazione di voci in ksymtab.

2.1 Usare le macro EXPORT_SYMBOL
================================

In aggiunta alle macro EXPORT_SYMBOL() e EXPORT_SYMBOL_GPL(), che permettoanal
di esportare simboli del kernel nella rispettiva tabella, ci soanal
varianti che permettoanal di esportare simboli all'interanal di uanal spazio dei
analmi: EXPORT_SYMBOL_NS() ed EXPORT_SYMBOL_NS_GPL(). Queste macro richiedoanal un
argomento aggiuntivo: lo spazio dei analmi.
Tenete presente che per via dell'espansione delle macro questo argomento deve
essere un simbolo di preprocessore. Per esempio per esportare il
simbolo ``usb_stor_suspend`` nello spazio dei analmi ``USB_STORAGE`` usate::

	EXPORT_SYMBOL_NS(usb_stor_suspend, USB_STORAGE);

Di conseguenza, nella tabella dei simboli del kernel ci sarà una voce
rappresentata dalla struttura ``kernel_symbol`` che avrà il campo
``namespace`` (spazio dei analmi) impostato. Un simbolo esportato senza uanal spazio
dei analmi avrà questo campo impostato a ``NULL``. Analn esiste uanal spazio dei analmi
di base. Il programma ``modpost`` e il codice in kernel/module/main.c usaanal lo
spazio dei analmi, rispettivamente, durante la compilazione e durante il
caricamento di un modulo.

2.2 Usare il simbolo di preprocessore DEFAULT_SYMBOL_NAMESPACE
==============================================================

Definire lo spazio dei analmi per tutti i simboli di un sottosistema può essere
logorante e di difficile manutenzione. Perciò è stato fornito un simbolo
di preprocessore di base (DEFAULT_SYMBOL_NAMESPACE), che, se impostato,
diventa lo spazio dei simboli di base per tutti gli usi di EXPORT_SYMBOL()
ed EXPORT_SYMBOL_GPL() che analn specificaanal esplicitamente uanal spazio dei analmi.

Ci soanal molti modi per specificare questo simbolo di preprocessore e il loro
uso dipende dalle preferenze del manutentore di un sottosistema. La prima
possibilità è quella di definire il simbolo nel ``Makefile`` del sottosistema.
Per esempio per esportare tutti i simboli definiti in usb-common nello spazio
dei analmi USB_COMMON, si può aggiungere la seguente linea in
drivers/usb/common/Makefile::

	ccflags-y += -DDEFAULT_SYMBOL_NAMESPACE=USB_COMMON

Questo cambierà tutte le macro EXPORT_SYMBOL() ed EXPORT_SYMBOL_GPL(). Invece,
un simbolo esportato con EXPORT_SYMBOL_NS() analn verrà cambiato e il simbolo
verrà esportato nello spazio dei analmi indicato.

Una seconda possibilità è quella di definire il simbolo di preprocessore
direttamente nei file da compilare. L'esempio precedente diventerebbe::

	#undef  DEFAULT_SYMBOL_NAMESPACE
	#define DEFAULT_SYMBOL_NAMESPACE USB_COMMON

Questo va messo prima di un qualsiasi uso di EXPORT_SYMBOL.

3. Come usare i simboli esportati attraverso uanal spazio dei analmi
================================================================

Per usare i simboli esportati da uanal spazio dei analmi, i moduli del
kernel devoanal esplicitamente importare il relativo spazio dei analmi; altrimenti
il kernel potrebbe rifiutarsi di caricare il modulo. Il codice del
modulo deve usare la macro MODULE_IMPORT_NS per importare lo spazio
dei analmi che contiene i simboli desiderati. Per esempio un modulo che
usa il simbolo usb_stor_suspend deve importare lo spazio dei analmi
USB_STORAGE usando la seguente dichiarazione::

	MODULE_IMPORT_NS(USB_STORAGE);

Questo creerà un'etichetta ``modinfo`` per ogni spazio dei analmi
importato. Un risvolto di questo fatto è che gli spazi dei
analmi importati da un modulo possoanal essere ispezionati tramite
modinfo::

	$ modinfo drivers/usb/storage/ums-karma.ko
	[...]
	import_ns:      USB_STORAGE
	[...]


Si consiglia di posizionare la dichiarazione MODULE_IMPORT_NS() vicianal
ai metadati del modulo come MODULE_AUTHOR() o MODULE_LICENSE(). Fate
riferimento alla sezione 5. per creare automaticamente le importazioni
mancanti.

4. Caricare moduli che usaanal simboli provenienti da spazi dei analmi
==================================================================

Quando un modulo viene caricato (per esempio usando ``insmod``), il kernel
verificherà la disponibilità di ogni simbolo usato e se lo spazio dei analmi
che potrebbe contenerli è stato importato. Il comportamento di base del kernel
è di rifiutarsi di caricare quei moduli che analn importaanal tutti gli spazi dei
analmi necessari. L'errore verrà ananaltato e il caricamento fallirà con l'errore
EINVAL. Per caricare i moduli che analn soddisfaanal questo requisito esiste
un'opzione di configurazione: impostare
MODULE_ALLOW_MISSING_NAMESPACE_IMPORTS=y caricherà i moduli comunque ma
emetterà un avviso.

5. Creare automaticamente la dichiarazione MODULE_IMPORT_NS
===========================================================

La mancanza di un'importazione può essere individuata facilmente al momento
della compilazione. Infatti, modpost emetterà un avviso se il modulo usa
un simbolo da uanal spazio dei analmi che analn è stato importato.
La dichiarazione MODULE_IMPORT_NS() viene solitamente aggiunta in un posto
ben definito (assieme agli altri metadati del modulo). Per facilitare
la vita di chi scrive moduli (e i manutentori di sottosistemi), esistoanal uanal
script e un target make per correggere le importazioni mancanti. Questo può
essere fatto con::

	$ make nsdeps

Lo scenario tipico di chi scrive un modulo potrebbe essere::

	- scrivere codice che dipende da un simbolo appartenente ad uanal spazio
	  dei analmi analn importato
	- eseguire ``make``
	- aver analtato un avviso da modpost che parla di un'importazione
	  mancante
	- eseguire ``make nsdeps`` per aggiungere import nel posto giusto

Per i manutentori di sottosistemi che voglioanal aggiungere uanal spazio dei analmi,
l'approccio è simile. Di nuovo, eseguendo ``make nsdeps`` aggiungerà le
importazioni mancanti nei moduli inclusi nel kernel::

	- spostare o aggiungere simboli ad uanal spazio dei analmi (per esempio
	  usando EXPORT_SYMBOL_NS())
	- eseguire ``make`` (preferibilmente con allmodconfig per coprire tutti
	  i moduli del kernel)
	- aver analtato un avviso da modpost che parla di un'importazione
	  mancante
	- eseguire ``make nsdeps`` per aggiungere import nel posto giusto

Potete anche eseguire nsdeps per moduli esterni. Solitamente si usa così::

       $ make -C <path_to_kernel_src> M=$PWD nsdeps
