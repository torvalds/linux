.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-ita.rst

Verificare la necessità di aggiornare le traduzioni
===================================================

Questo script aiuta a tracciare lo stato delle traduzioni della
documentazione nelle diverse lingue, ovvero se la documentazione è
allineata con la controparte inglese.

Come funziona
-------------

Lo script usa il comando ``git log`` per individuare l'ultimo commit in inglese
a partire dal commit della traduzione (in ordine di data dell'autore) e gli
ultimi commit in inglese a partire da HEAD. Se emergono delle differenze, il
file viene considerato non aggiornato, e vengono quindi raccolti e segnalati i
commit che necessitano di un aggiornamento.

Funzionalità implementate

-  verifica di tutti i file in una determinata lingua
-  verifica di un singolo file o di un insieme di file
-  opzioni per modificare il formato dell'output
-  tracciamento dello stato di traduzione dei file che non hanno alcuna
   traduzione

Utilizzo
--------

::

   tools/docs/checktransupdate.py --help

Fate riferimento all'output del messaggio d'aiuto per i dettagli sull'utilizzo.

Esempi

-  ``tools/docs/checktransupdate.py -l zh_CN``
   Questo stamperà tutti i file che necessitano di un aggiornamento nella
   lingua zh_CN.
-  ``tools/docs/checktransupdate.py Documentation/translations/zh_CN/dev-tools/testing-overview.rst``
   Questo stamperà solamente lo stato del file specificato.

L'output sarà quindi qualcosa del genere:

::

    Documentation/dev-tools/kfence.rst
    No translation in the locale of zh_CN

    Documentation/translations/zh_CN/dev-tools/testing-overview.rst
    commit 42fb9cfd5b18 ("Documentation: dev-tools: Add link to RV docs")
    1 commits needs resolving in total

Funzionalità ancora da implementare

- specificare cartelle in aggiunta ai singoli file
