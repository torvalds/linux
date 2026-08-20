.. include:: ../disclaimer-ita.rst

=====================================
Includere i file di intestazione uAPI
=====================================

Qualche volta è utile includere dei file di intestazione e degli esempi di codice C
al fine di descrivere l'API per lo spazio utente e per generare dei riferimenti
fra il codice e la documentazione. Aggiungere i riferimenti ai file dell'API
dello spazio utente ha un ulteriore vantaggio: Sphinx genererà dei messaggi
d'avviso se un simbolo non viene trovato nella documentazione. Questo permette
di mantenere allineate la documentazione della uAPI (API spazio utente)
con le modifiche del kernel.
Il programma :ref:`parse_headers.py <it_parse_headers>` genera questi
riferimenti. Esso dev'essere invocato attraverso un Makefile, mentre si genera
la documentazione. Per avere un esempio su come utilizzarlo all'interno del
kernel consultate ``Documentation/userspace-api/media/Makefile``.

.. _it_parse_headers:

tools/docs/parse_headers.py
^^^^^^^^^^^^^^^^^^^^^^^^^^^

NOME
****

parse_headers.py - analizza un file C al fine di identificare funzioni,
strutture, enumerati e definizioni, e creare riferimenti per un libro Sphinx.

USO
***

parse-headers.py [-h] [-d] [-t] ``FILE_IN`` ``FILE_OUT`` ``FILE_RULES``

SINOSSI
*******

Converte un file d'intestazione o un file sorgente C ``FILE_IN`` in un testo
ReStructured Text incluso mediante il blocco ..parsed-literal con riferimenti
alla documentazione che descrive l'API. Accetta opzionalmente un file
``FILE_RULES`` che descrive quali elementi debbano essere ignorati o il cui
riferimento debba puntare ad un tipo/nome diverso da quello predefinito.

Il file generato viene scritto in ``FILE_OUT``.

Il programma è capace di identificare ``define``, ``struct``, ``typedef``,
``enum`` e ``symbol`` di un enumerato, creando i riferimenti per ognuno di
loro.

Inoltre, esso è capace di distinguere le ``#define`` utilizzate per
specificare le macro specifiche di Linux usate per definire gli ``ioctl``.

Il file ``FILE_RULES``, opzionale, contiene un insieme di regole come le
seguenti::

    ignore ioctl VIDIOC_ENUM_FMT
    replace ioctl VIDIOC_DQBUF vidioc_qbuf
    replace define V4L2_EVENT_MD_FL_HAVE_FRAME_SEQ :c:type:`v4l2_event_motion_det`

ARGOMENTI POSIZIONALI
*********************

  ``FILE_IN``
      File C d'ingresso

  ``FILE_OUT``
      File RST generato

  ``FILE_RULES``
      File delle eccezioni (opzionale)

OPZIONI
*******

  ``-h``, ``--help``
      mostra un messaggio d'aiuto e termina
  ``-d``, ``--debug``
      aumenta il livello di debug. Può essere usato più volte
  ``-t``, ``--toc``
      invece di un blocco letterale, genera nel file RST una tabella
      dell'indice (TOC)


DESCRIZIONE
***********

Crea, a partire da ``FILE_IN``, una versione arricchita di un file
d'intestazione del kernel con collegamenti incrociati verso ogni tipo di
struttura dati C, formattandola con la notazione reStructuredText, sia
come blocco letterale che come tabella dell'indice.

Accetta opzionalmente un file ``FILE_RULES`` che descrive quali elementi
debbano essere ignorati o il cui riferimento debba puntare ad un valore
diverso da quello predefinito, e che può opzionalmente definire lo spazio
dei nomi C da utilizzare.

Ha lo scopo di permettere una documentazione più completa, in cui i file
d'intestazione della uAPI creino collegamenti incrociati verso il codice.

Il file generato viene scritto in ``FILE_OUT``.

Il file ``FILE_RULES`` può contenere tre tipi di dichiarazioni:
**ignore**, **replace** e **namespace**.

Per impostazione predefinita, vengono create regole per tutti i simboli e
le definizioni, ma è anche possibile fornire un file di eccezioni. Questo
file contiene un insieme di regole che seguono la sintassi descritta di
seguito:

1. Regole ignore:

    ignore *tipo* *simbolo*

Rimuove il simbolo dalla generazione dei riferimenti.

2. Regole replace:

    replace *tipo* *vecchio_simbolo* *nuovo_riferimento*

    Sostituisce *vecchio_simbolo* con *nuovo_riferimento*.
    *nuovo_riferimento* può essere:

    - un semplice nome di simbolo;
    - un riferimento Sphinx completo.

3. Regole namespace

    namespace *spazio_dei_nomi*

    Imposta lo *spazio_dei_nomi* C da utilizzare durante la generazione dei
    riferimenti incrociati. Può essere sovrascritto dalle regole replace.

Nelle regole ignore e replace, *tipo* può essere:

    - ioctl:
        per le definizioni della forma ``_IO*``, per esempio le definizioni
        di ioctl

    - define:
        per le altre definizioni

    - symbol:
        per i simboli definiti all'interno di enumerati;

    - typedef:
        per i typedef;

    - enum:
        per il nome di un enumerato non anonimo;

    - struct:
        per le strutture.


ESEMPI
******

- Ignora una definizione ``_VIDEODEV2_H`` in ``FILE_IN``::

    ignore define _VIDEODEV2_H

- In una struttura dati come questo enumerato::

    enum foo { BAR1, BAR2, PRIVATE };

  Non genererà alcun riferimento incrociato per ``PRIVATE``::

    ignore symbol PRIVATE

  Nello stesso enumerato, invece di creare un riferimento incrociato per
  ogni simbolo, si può far si che tutti puntino al tipo C ``enum foo``::

    replace symbol BAR1 :c:type:\`foo\`
    replace symbol BAR2 :c:type:\`foo\`


- Usa lo spazio dei nomi C ``MC`` per tutti i simboli in ``FILE_IN``::

    namespace MC

BUGS
****

Segnalate qualsiasi malfunzionamento a Mauro Carvalho Chehab
<mchehab@kernel.org>

COPYRIGHT
*********

Copyright (c) 2016, 2025 di Mauro Carvalho Chehab <mchehab+huawei@kernel.org>.

Licenza GPLv2: GNU GPL versione 2 <https://gnu.org/licenses/gpl.html>.

Questo è software libero: siete liberi di cambiarlo e ridistribuirlo.
Non c'è alcuna garanzia, nei limiti permessi dalla legge.
