.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-ita.rst

Profilo del manutentore del sottosistema di documentazione
==========================================================

Il "sottosistema" della documentazione è il punto di coordinamento
centrale per la documentazione del kernel e la relativa infrastruttura.
Copre la gerarchia sotto Documentation/ (con l'eccezione di
Documentation/devicetree), diverse utilità sotto scripts/ e, almeno in
parte, LICENSES/.

Vale la pena notare, però, che i confini di questo sottosistema sono più sfumati
del normale. Molti altri manutentori di sottosistemi preferiscono mantenere il
controllo di alcune parti di Documentation/, e molti altri ancora vi applicano
liberamente delle modifiche quando è conveniente. Oltre a ciò, buona parte della
documentazione del kernel si trova nel codice sorgente sotto forma di commenti
kerneldoc; questi sono solitamente (ma non sempre) mantenuti dal manutentore del
sottosistema pertinente.

La lista di discussione per la documentazione è linux-doc@vger.kernel.org.
Le patch dovrebbero essere inviate contro l'albero docs-next quando
possibile.

Aggiunta alla checklist di invio
--------------------------------

Quando si apportano modifiche alla documentazione, dovreste generare
effettivamente la documentazione e assicurarvi che non siano stati
introdotti nuovi errori o avvisi. Generare i documenti in HTML e osservare
il risultato aiuterà a evitare fraintendimenti spiacevoli su come le cose
verranno rappresentate.

Tutta la nuova documentazione (incluse le aggiunte a documenti esistenti)
dovrebbe idealmente giustificare, da qualche parte nel changelog, chi sia
il pubblico a cui è destinata; in questo modo, ci assicuriamo che la
documentazione finisca nel posto giusto. Alcune categorie possibili
sono: sviluppatori del kernel (esperti o principianti), programmatori
dello spazio utente, utenti finali e/o amministratori di sistema, e
distributori.

Date chiave del ciclo
---------------------

Le patch possono essere inviate in qualsiasi momento, ma la risposta sarà
più lenta del solito durante la finestra d'integrazione. L'albero della
documentazione tende a chiudersi tardi, prima dell'apertura della finestra
d'integrazione, poiché il rischio di regressioni dovute a patch sulla
documentazione è basso.

Cadenza di revisione
--------------------

Sono (Jonathan Corbet) l'unico manutentore del sottosistema di documentazione, e
svolgo questo lavoro nel mio tempo libero, quindi la risposta alle patch sarà a
volte lenta. Cerco sempre di inviare una notifica quando una patch viene
integrata (o quando decido che non può esserlo). Non esitate a inviare un
sollecito se non avete ricevuto risposta entro una settimana dall'invio di una
patch.
