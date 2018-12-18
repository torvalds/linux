.. include:: ../disclaimer-ita.rst

.. note:: Per leggere la documentazione originale in inglese:
	  :ref:`Documentation/doc-guide/index.rst <doc_guide>`

=========================================
Includere gli i file di intestazione uAPI
=========================================

Qualche volta è utile includere dei file di intestazione e degli esempi di codice C
al fine di descrivere l'API per lo spazio utente e per generare dei riferimenti
fra il codice e la documentazione. Aggiungere i riferimenti ai file dell'API
dello spazio utente ha ulteriori vantaggi: Sphinx genererà dei messaggi
d'avviso se un simbolo non viene trovato nella documentazione. Questo permette
di mantenere allineate la documentazione della uAPI (API spazio utente)
con le modifiche del kernel.
Il programma :ref:`parse_headers.pl <it_parse_headers>` genera questi riferimenti.
Esso dev'essere invocato attraverso un Makefile, mentre si genera la
documentazione. Per avere un esempio su come utilizzarlo all'interno del kernel
consultate ``Documentation/media/Makefile``.

.. _it_parse_headers:

parse_headers.pl
^^^^^^^^^^^^^^^^

NOME
****


parse_headers.pl - analizza i file C al fine di identificare funzioni,
strutture, enumerati e definizioni, e creare riferimenti per Sphinx

SINTASSI
********


\ **parse_headers.pl**\  [<options>] <C_FILE> <OUT_FILE> [<EXCEPTIONS_FILE>]

Dove <options> può essere: --debug, --usage o --help.


OPZIONI
*******



\ **--debug**\

 Lo script viene messo in modalità verbosa, utile per il debugging.


\ **--usage**\

 Mostra un messaggio d'aiuto breve e termina.


\ **--help**\

 Mostra un messaggio d'aiuto dettagliato e termina.


DESCRIZIONE
***********

Converte un file d'intestazione o un file sorgente C (C_FILE) in un testo
ReStructuredText incluso mediante il blocco ..parsed-literal
con riferimenti alla documentazione che descrive l'API. Opzionalmente,
il programma accetta anche un altro file (EXCEPTIONS_FILE) che
descrive quali elementi debbano essere ignorati o il cui riferimento
deve puntare ad elemento diverso dal predefinito.

Il file generato sarà disponibile in (OUT_FILE).

Il programma è capace di identificare *define*, funzioni, strutture,
tipi di dato, enumerati e valori di enumerati, e di creare i riferimenti
per ognuno di loro. Inoltre, esso è capace di distinguere le #define
utilizzate per specificare i comandi ioctl di Linux.

Il file EXCEPTIONS_FILE contiene due tipi di dichiarazioni:
\ **ignore**\  o \ **replace**\ .

La sintassi per ignore è:

ignore \ **tipo**\  \ **nome**\

La dichiarazione \ **ignore**\  significa che non verrà generato alcun
riferimento per il simbolo \ **name**\  di tipo \ **tipo**\ .


La sintassi per replace è:

replace \ **tipo**\  \ **nome**\  \ **nuovo_valore**\

La dichiarazione \ **replace**\  significa che verrà generato un
riferimento per il simbolo \ **name**\ di tipo \ **tipo**\ , ma, invece
di utilizzare il valore predefinito, verrà utilizzato il valore
\ **nuovo_valore**\ .

Per entrambe le dichiarazioni, il \ **tipo**\  può essere uno dei seguenti:


\ **ioctl**\

 La dichiarazione ignore o replace verrà applicata su definizioni di ioctl
 come la seguente:

 #define	VIDIOC_DBG_S_REGISTER 	 _IOW('V', 79, struct v4l2_dbg_register)



\ **define**\

 La dichiarazione ignore o replace verrà applicata su una qualsiasi #define
 trovata in C_FILE.



\ **typedef**\

 La dichiarazione ignore o replace verrà applicata ad una dichiarazione typedef
 in C_FILE.



\ **struct**\

 La dichiarazione ignore o replace verrà applicata ai nomi di strutture
 in C_FILE.



\ **enum**\

 La dichiarazione ignore o replace verrà applicata ai nomi di enumerati
 in C_FILE.



\ **symbol**\

 La dichiarazione ignore o replace verrà applicata ai nomi di valori di
 enumerati in C_FILE.

 Per le dichiarazioni di tipo replace, il campo \ **new_value**\  utilizzerà
 automaticamente i riferimenti :c:type: per \ **typedef**\ , \ **enum**\  e
 \ **struct**\. Invece, utilizzerà :ref: per \ **ioctl**\ , \ **define**\  e
 \ **symbol**\. Il tipo di riferimento può essere definito esplicitamente
 nella dichiarazione stessa.


ESEMPI
******


ignore define _VIDEODEV2_H


Ignora una definizione #define _VIDEODEV2_H nel file C_FILE.

ignore symbol PRIVATE


In un enumerato come il seguente:

enum foo { BAR1, BAR2, PRIVATE };

Non genererà alcun riferimento per \ **PRIVATE**\ .

replace symbol BAR1 :c:type:\`foo\`
replace symbol BAR2 :c:type:\`foo\`


In un enumerato come il seguente:

enum foo { BAR1, BAR2, PRIVATE };

Genererà un riferimento ai valori BAR1 e BAR2 dal simbolo foo nel dominio C.


BUGS
****

Riferire ogni malfunzionamento a Mauro Carvalho Chehab <mchehab@s-opensource.com>


COPYRIGHT
*********


Copyright (c) 2016 by Mauro Carvalho Chehab <mchehab@s-opensource.com>.

Licenza GPLv2: GNU GPL version 2 <http://gnu.org/licenses/gpl.html>.

Questo è software libero: siete liberi di cambiarlo e ridistribuirlo.
Non c'è alcuna garanzia, nei limiti permessi dalla legge.
