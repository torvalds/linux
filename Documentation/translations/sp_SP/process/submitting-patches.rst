.. include:: ../disclaimer-sp.rst

:Original: :ref:`Documentation/process/submitting-patches.rst <submittingpatches>`
:Translator: Carlos Bilbao <carlos.bilbao.osdev@gmail.com>

.. _sp_submittingpatches:

Envío de parches: la guía esencial para incluir su código en el kernel
=======================================================================

Para una persona o empresa que desee enviar un cambio al kernel Linux,
el proceso puede en ocasiones resultar desalentador si no se está
familiarizado con "el sistema". Este texto es una colección de sugerencias
que pueden aumentar considerablemente las posibilidades de que se acepte su
cambio.

Este documento contiene una gran cantidad de sugerencias en un formato
relativamente conciso. Para obtener información detallada sobre cómo
funciona el proceso de desarrollo del kernel, consulte
Documentation/process/development-process.rst. Además, lea
Documentation/process/submit-checklist.rst para obtener una lista de
elementos a verificar antes de enviar código. Para los parches de
"binding" del árbol de dispositivos, lea
Documentation/devicetree/bindings/submitting-patches.rst.

Esta documentación asume que está usando ``git`` para preparar sus parches.
Si no está familiarizado con ``git``, le recomendamos que aprenda a
usarlo, le hará la vida como desarrollador del kernel y en general mucho
más sencilla.

Algunos subsistemas y árboles de mantenimiento cuentan con información
adicional sobre su flujo de trabajo y expectativas, consulte
:ref:`Documentation/process/maintainer-handbooks.rst <maintainer_handbooks_main>`.

Obtenga el código fuente actual
--------------------------------

Si no tiene a mano un repositorio con el código fuente actual del kernel,
use ``git`` para obtener uno. Querrá comenzar con el repositorio principal,
que se puede descargar con::

  git clone git://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git

Tenga en cuenta, sin embargo, que es posible que no desee desarrollar con
el árbol principal directamente. La mayoría de los maintainers de
subsistemas usan sus propios árboles de código fuente y quieren ver parches
preparados para esos árboles. Revise el campo **T:** para el subsistema
en el archivo MAINTAINERS para encontrar dicho árbol, o simplemente
pregunte al maintainer si el árbol no está listado allí.

.. _sp_describe_changes:

Describa sus cambios
---------------------

Describa su problema. Sea su parche una corrección de un error de una
línea o 5000 líneas para una nuevo "feature", debe haber un problema
subyacente que le motivó a hacer ese trabajo. Convenza al revisor de que
hay un problema que merece la pena solucionar y de que tiene sentido que
lea más allá del primer párrafo.

Describa el impacto relativo al usuario. Cosas que estropeen el kernel y
los bloqueos son bastante convincentes, pero no todos los errores son tan
evidentes. Incluso si se detectó un problema durante la revisión del
código, describa el impacto que cree pueda tener en los usuarios. Tenga en
cuenta que la mayoría de instalaciones de Linux ejecutan kernels desde
árboles estables secundarios o árboles específicos de proveedor/producto
que seleccionan ("cherry-pick") solo parches específicos de upstream, así
que incluya cualquier cosa que pueda ayudar a dirigir su cambio
aguas abajo: circunstancias que producen cierta situación, extractos de
dmesg, descripciones del error fatal, regresiones de rendimiento, picos de
latencia, bloqueos, etc.

Cuantifique optimizaciones y beneficios/perdidas. Si asegura mejoras en
rendimiento, consumo de memoria, huella del stack o tamaño de binario,
incluya números que lo respalden. Pero también describa costes no obvios.
Las optimizaciones generalmente no son gratuitas, sino un equilibrio entre
CPU, memoria y legibilidad; o, cuando se trata de heurísticas, entre
diferentes cargas de trabajo. Describa las desventajas esperadas de su
optimización para que el revisor pueda comparar las perdidas con los
beneficios.

Una vez establecido el problema, describa lo que realmente está haciendo
al respecto en detalles técnicos. Es importante describir el cambio en
lenguaje sencillo para que el revisor verifique que el código se está
comportando como se pretende.

El maintainer le agradecerá que escriba la descripción de su parche en un
formato que se pueda incorporar fácilmente en la gestión del código fuente
del sistema, ``git``, como un "commit log" (registros de los commits).
Consulte :ref:`sp_the_canonical_patch_format`.

Resuelva solo un problema por parche. Si su descripción comienza a ser muy
larga, eso es una señal de que probablemente necesite dividir su parche.
Lea :ref:`split_changes`.

Cuando envíe o vuelva a enviar un parche o una serie de parches, incluya la
descripción completa del parche y justificación del mismo. No se limite a
decir que esa es la versión N del parche (serie). No espere que el
maintainer del subsistema referencie versiones de parches anteriores o use
referencias URL para encontrar la descripción del parche y colocarla en el
parche. Es decir, el parche (serie) y su descripción deben ser
independientes. Esto beneficia tanto a los maintainers como a los
revisores. Algunos revisores probablemente ni siquiera recibieran versiones
anteriores del parche.

Describa sus cambios en la forma imperativa, por ejemplo, "hacer que xyzzy
haga frotz" en lugar de "[Este parche] hace que xyzzy haga frotz" o "[Yo]
Cambié xyzzy para que haga frotz", como si estuviera dando órdenes al
código fuente para cambiar su comportamiento.

Si desea hacer referencia a un commit específico, no se limite a hacer
referencia al ID SHA-1 del commit. Incluya también el resumen de una línea
del commit, para que sea más fácil para los revisores saber de qué se
trata.
Ejemplo::

	Commit e21d2170f36602ae2708 ("video: quitar platform_set_drvdata()
	innecesario") eliminó innecesario platform_set_drvdata(), pero dejó la
	variable "dev" sin usar, bórrese.

También debe asegurarse de utilizar al menos los primeros doce caracteres
del identificador SHA-1. El repositorio del kernel contiene muchos *muchos*
objetos, por lo que las colisiones con identificaciones más cortas son una
posibilidad real. Tenga en cuenta que, aunque no hay colisión con su
identificación de seis caracteres ahora, esa condición puede cambiar dentro
de cinco años.

Si las discusiones relacionadas o cualquier otra información relativa al
cambio se pueden encontrar en la web, agregue las etiquetas 'Link:' que
apunten a estos. En caso de que su parche corrija un error, por poner un
ejemplo, agregue una etiqueta con una URL que haga referencia al informe en
los archivos de las listas de correo o un rastreador de errores; si el
parche es el resultado de alguna discusión anterior de la lista de correo o
algo documentado en la web, referencie esto.

Cuando se vincule a archivos de listas de correo, preferiblemente use el
servicio de archivador de mensajes lore.kernel.org. Para crear la URL del
enlace, utilice el contenido del encabezado ("header") ``Message-ID`` del
mensaje sin los corchetes angulares que lo rodean.
Por ejemplo::

    Link: https://lore.kernel.org/30th.anniversary.repost@klaava.Helsinki.FI

Verifique el enlace para asegurarse de que realmente funciona y apunta al
mensaje correspondiente.

Sin embargo, intente que su explicación sea comprensible sin recursos
externos. Además de dar una URL a un archivo o error de la lista de correo,
resuma los puntos relevantes de la discusión que condujeron al parche tal y
como se envió.

Si su parche corrige un error en un commit específico, por ejemplo
encontró un problema usando ``git bisect``, utilice la etiqueta 'Fixes:'
con los primeros 12 caracteres del ID SHA-1 y el resumen de una línea. No
divida la etiqueta en varias líneas, las etiquetas están exentas de la
regla "ajustar a 75 columnas" para simplificar análisis de scripts. Por
ejemplo::

	Fixes: 54a4f0239f2e ("KVM: MMU: hacer que kvm_mmu_zap_page()
	devuelva la cantidad de páginas que realmente liberó")

Las siguientes configuraciones de ``git config`` se pueden usar para
agregar un bonito formato y generar este estilo con los comandos
``git log`` o ``git show``::

	[core]
		abbrev = 12
	[pretty]
		fixes = Fixes: %h (\"%s\")

Un ejemplo de uso::

	$ git log -1 --pretty=fixes 54a4f0239f2e
	Fixes: 54a4f0239f2e ("KVM: MMU: hacer que kvm_mmu_zap_page() devuelva la cantidad de páginas que realmente liberó")

.. _sp_split_changes:

Separe sus cambios
-------------------

Separe cada **cambio lógico** en un parche separado.

Por ejemplo, si sus cambios incluyen correcciones de errores y mejoras en
el rendimiento de un controlador, separe esos cambios en dos o más parches.
Si sus cambios incluyen una actualización de la API y una nueva controlador
que usa esta nueva API, sepárelos en dos parches.

Por otro lado, si realiza un solo cambio en numerosos archivos, agrupe esos
cambios en un solo parche. Por lo tanto, un solo cambio lógico estará
contenido en un solo parche.

El punto a recordar es que cada parche debe realizar un cambio que puede
ser verificado por los revisores fácilmente. Cada parche debe ser
justificable por sus propios méritos.

Si un parche depende de otro parche para que un cambio sea completo, eso
está bien. Simplemente incluya que **"este parche depende del parche X"**
en la descripción de su parche.

Cuando divida su cambio en una serie de parches, tenga especial cuidado en
asegurarse de que el kernel se compila y ejecuta correctamente después de
cada parche en la serie. Los desarrolladores que usan ``git bisect``
para rastrear un problema pueden terminar dividiendo su serie de parches en
cualquier punto; no le agradecerán si introdujo errores a la mitad.

Si no puede condensar su conjunto de parches en un conjunto más pequeño de
parches, solo publique, más o menos 15 a la vez, y espere la revisión e
integración.


Revise el estilo en sus cambios
--------------------------------

Revise su parche para ver si hay violaciones de estilo básico, cuyos
detalles pueden ser encontrados en Documentation/process/coding-style.rst.
No hacerlo simplemente desperdicia el tiempo de los revisores y su parche
será rechazado, probablemente sin siquiera ser leído.

Una excepción importante es cuando se mueve código de un archivo a otro.
En tal caso, en absoluto debe modificar el código movido en el mismo parche
en que lo mueve. Esto divide claramente el acto de mover el código y sus
cambios. Esto ayuda mucho a la revisión de la diferencias reales y permite
que las herramientas rastreen mejor el historial del código en sí.

Verifique sus parches con el verificador de estilo de parches antes de
enviarlos (scripts/checkpatch.pl). Tenga en cuenta, sin embargo, que el
verificador de estilo debe ser visto como una guía, no como un reemplazo
del juicio humano. Si su código es mejor con una violación entonces
probablemente sea mejor dejarlo estar.

El verificador informa a tres niveles:
 - ERROR: cosas que es muy probable que estén mal
 - WARNING: Advertencia. Cosas que requieren una revisión cuidadosa
 - CHECK: Revisar. Cosas que requieren pensarlo

Debe poder justificar todas las violaciones que permanezcan en su parche.


Seleccione los destinatarios de su parche
------------------------------------------

Siempre debe incluir en copia a los apropiados maintainers del subsistema
en cualquier parche con código que mantengan; revise a través del archivo
MAINTAINERS y el historial de revisión del código fuente para ver quiénes
son esos maintainers. El script scripts/get_maintainer.pl puede ser muy
útil en este paso (pase rutas a sus parches como argumentos para
scripts/get_maintainer.pl). Si no puede encontrar un maintainer del
subsistema en el que está trabajando, Andrew Morton
(akpm@linux-foundation.org) sirve como maintainer de último recurso.

Normalmente, también debe elegir al menos una lista de correo para recibir
una copia de su conjunto de parches. linux-kernel@vger.kernel.org debe
usarse de forma predeterminada para todos los parches, pero el volumen en
esta lista ha hecho que muchos desarrolladores se desconecten. Busque en el
archivo MAINTAINERS una lista específica de los subsistemas; su parche
probablemente recibirá más atención allí. Sin embargo, no envíe spam a
listas no relacionadas.

Muchas listas relacionadas con el kernel están alojadas en kernel.org;
puedes encontrar un listado de estas en
https://subspace.kernel.org. Existen listas relacionadas con el kernel
alojadas en otros lugares, no obstante.

¡No envíe más de 15 parches a la vez a las listas de correo de vger!

Linus Torvalds es el árbitro final de todos los cambios aceptados en el
kernel de Linux. Su dirección de correo electrónico es
<torvalds@linux-foundation.org>. Recibe muchos correos electrónicos y, en
este momento, muy pocos parches pasan por Linus directamente, por lo que
normalmente debe hacer todo lo posible para -evitar- enviarle un correo
electrónico.

Si tiene un parche que corrige un error de seguridad explotable, envíe ese
parche a security@kernel.org. Para errores graves, se debe mantener un
poco de discreción y permitir que los distribuidores entreguen el parche a
los usuarios; en esos casos, obviamente, el parche no debe enviarse a
ninguna lista pública. Revise también
Documentation/process/security-bugs.rst.

Los parches que corrigen un error grave en un kernel en uso deben dirigirse
hacia los maintainers estables poniendo una línea como esta::

  CC: stable@vger.kernel.org

en el área de sign-off de su parche (es decir, NO un destinatario de correo
electrónico). También debe leer
Documentation/process/stable-kernel-rules.rst además de este documento.

Si los cambios afectan las interfaces del kernel para el usuario, envíe al
maintainer de las MAN-PAGES (como se indica en el archivo MAINTAINERS) un
parche de páginas de manual, o al menos una notificación del cambio, para
que alguna información se abra paso en las páginas del manual. Los cambios
de la API del espacio de usuario también deben copiarse en
linux-api@vger.kernel.org.


Sin MIME, enlaces, compresión o archivos adjuntos. Solo texto plano
--------------------------------------------------------------------

Linus y otros desarrolladores del kernel deben poder leer y comentar sobre
los cambios que está enviando. Es importante para un desarrollador kernel
poder "citar" sus cambios, utilizando herramientas estándar de correo
electrónico, de modo que puedan comentar sobre partes específicas de su
código.

Por este motivo, todos los parches deben enviarse por correo electrónico
"inline". La forma más sencilla de hacerlo es con ``git send-email``, que
es muy recomendable. Un tutorial interactivo para ``git send-email`` está
disponible en https://git-send-email.io.

Si elige no usar ``git send-email``:

.. warning::

  Tenga cuidado con el ajuste de palabras de su editor que corrompe su
  parche, si elige cortar y pegar su parche.

No adjunte el parche como un archivo adjunto MIME, comprimido o no. Muchas
populares aplicaciones de correo electrónico no siempre transmiten un MIME
archivo adjunto como texto sin formato, por lo que es imposible comentar
en su código. Linus también necesita un poco más de tiempo para procesar un
archivo adjunto MIME, disminuyendo la probabilidad de que se acepte su
cambio adjunto en MIME.

Excepción: si su proveedor de correo está destrozando parches, entonces
alguien puede pedir que los vuelva a enviar usando MIME.

Consulte Documentation/process/email-clients.rst para obtener sugerencias
sobre cómo configurar su cliente de correo electrónico para que envíe sus
parches intactos.

Responda a los comentarios de revisión
---------------------------------------

Es casi seguro que su parche recibirá comentarios de los revisores sobre
maneras en que se pueda mejorar el parche, en forma de respuesta a su
correo electrónico. Debe responder a esos comentarios; ignorar a los
revisores es una buena manera de ser ignorado de vuelta. Simplemente puede
responder a sus correos electrónicos para contestar a sus comentarios.
Revisiones a los comentarios o preguntas que no conduzcan a un cambio de
código deben casi con certeza generar un comentario o una entrada en el
"changelog" para que el próximo revisor entienda lo que está pasando.

Asegúrese de decirles a los revisores qué cambios está haciendo y de
agradecerles que dediquen su tiempo. La revisión del código es un proceso
agotador y lento, y los revisores a veces se ponen de mal humor. Sin
embargo, incluso en ese caso, responda cortésmente y aborde los problemas
que hayan señalado. Al enviar un siguiente versión, agregue un
``patch changelog`` (registro de cambios en los parches) a la carta de
presentación ("cover letter") o a parches individuales explicando la
diferencia con la presentación anterior (ver
:ref:`sp_the_canonical_patch_format`).

Consulte Documentation/process/email-clients.rst para obtener
recomendaciones sobre clientes de correo electrónico y normas de etiqueta
en la lista de correo.

.. _sp_interleaved_replies:

Uso de respuestas intercaladas recortadas en las discusiones por correo electrónico
-----------------------------------------------------------------------------------

Se desaconseja encarecidamente la publicación en la parte superior de las
discusiones sobre el desarrollo del kernel de Linux. Las respuestas
intercaladas (o "en línea") hacen que las conversaciones sean mucho más
fáciles de seguir. Para obtener más detalles, consulte:
https://en.wikipedia.org/wiki/Posting_style#Interleaved_style

Como se cita frecuentemente en la lista de correo::

  A: http://en.wikipedia.org/wiki/Top_post
  Q: ¿Dónde puedo encontrar información sobre esto que se llama top-posting?
  A: Porque desordena el orden en el que la gente normalmente lee el texto.
  Q: ¿Por qué es tan malo el top-posting?
  A: Top-posting.
  Q: ¿Qué es lo más molesto del correo electrónico?

Del mismo modo, por favor, recorte todas las citas innecesarias que no
sean relevantes para su respuesta. Esto hace que las respuestas sean más
fáciles de encontrar y ahorra tiempo y espacio. Para obtener más
información, consulte: http://daringfireball.net/2007/07/on_top ::

  A: No.
  Q: ¿Debo incluir citas después de mi respuesta?

.. _sp_resend_reminders:

No se desanime o impaciente
---------------------------

Después de haber entregado su cambio, sea paciente y espere. Los revisores
son personas ocupadas y es posible que no lleguen a su parche de inmediato.

Érase una vez, los parches solían desaparecer en el vacío sin comentarios,
pero el proceso de desarrollo funciona mejor que eso ahora. Debería
recibir comentarios dentro de una semana más o menos; si eso no sucede,
asegúrese de que ha enviado sus parches al lugar correcto. Espere un mínimo
de una semana antes de volver a enviar o hacer ping a los revisores,
posiblemente más durante periodos de mucho trabajo ocupados como "merge
windows".

También está bien volver a enviar el parche o la serie de parches después
de un par de semanas con la palabra "RESEND" (reenviar) añadida a la línea
de asunto::

   [PATCH Vx RESEND] sub/sys: Resumen condensado de parche

No incluya "RESEND" cuando envíe una versión modificada de su parche o
serie de parches: "RESEND" solo se aplica al reenvío de un parche o serie
de parches que no hayan sido modificados de ninguna manera con respecto a
la presentación anterior.


Incluya PATCH en el asunto
--------------------------

Debido al alto tráfico de correo electrónico a Linus y al kernel de Linux,
es común prefijar su línea de asunto con [PATCH]. Esto le permite a Linus
y otros desarrolladores del kernel distinguir más fácilmente los parches de
otras discusiones por correo electrónico.

``git send-email`` lo hará automáticamente.


Firme su trabajo: el Certificado de Origen del Desarrollador
------------------------------------------------------------

Para mejorar el seguimiento de quién hizo qué, especialmente con parches
que pueden filtrarse hasta su destino final a través de varias capas de
maintainers, hemos introducido un procedimiento de "sign-off" (aprobación)
en parches que se envían por correo electrónico.

La aprobación es una simple línea al final de la explicación del parche,
que certifica que usted lo escribió o que tiene derecho a enviarlo como un
parche de código abierto. Las reglas son bastante simples: si usted puede
certificar lo siguiente:

Certificado de Origen del Desarrollador 1.1
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Al hacer una contribución a este proyecto, certifico que:

        (a) La contribución fue creada en su totalidad o en parte por mí y
            tengo derecho a enviarlo bajo la licencia de código abierto
            indicada en el documento; o

        (b) La contribución se basa en trabajo previo que, hasta donde yo
            soy consciente, está cubierto por una licencia de código
            abierto apropiada y tengo el derecho bajo esa licencia de
            presentar tal trabajo con modificaciones, ya sean creadas en su
            totalidad o en parte por mí, bajo la misma licencia de código
            (salvo que sea permitido presentar bajo una licencia diferente),
            tal y como se indica en el documento; o

        (c) La contribución me fue proporcionada directamente por alguna
            otra persona que certificó (a), (b) o (c) y no he modificado
            esto.

        (d) Entiendo y acepto que este proyecto y la contribución
            son públicos y que un registro de la contribución (incluyendo
            toda la información personal que envío con él, incluida mi
            firma) es mantenida indefinidamente y puede ser redistribuida
            de manera consistente con este proyecto o la(s) licencia(s) de
            código abierto involucradas.

entonces simplemente incluya una línea que rece::

	Signed-off-by: Random J Developer <random@developer.example.org>

usando su nombre real (lamentablemente, no pseudónimos ni contribuciones
anónimas). Esto se hará por usted automáticamente si usa ``git commit -s``.
Las reversiones de código también deben incluir "Signed-off-by".
``git revert -s`` hace eso por usted.

Algunas personas también ponen etiquetas adicionales al final. Simplemente
serán ignoradas por ahora, pero puede hacer esto para marcar procedimientos
internos de su empresa o simplemente señalar algún detalle especial sobre
la firma.

Cualquier otro SoB (Signed-off-by:) después del SoB del autor es de
personas que manipulen y transporten el parche, pero no participaron en su
desarrollo. Las cadenas de SoB deben reflejar la ruta **real** del parche
de cómo se propagó a los maintainers y, en última instancia, a Linus, con
la primera entrada de SoB que señala la autoría principal de un solo autor.


Cuándo usar Acked-by:, Cc: y Co-developed-by por:
-------------------------------------------------

La etiqueta Signed-off-by: indica que el firmante estuvo involucrado en el
desarrollo del parche, o que él/ella se encontraba en el camino de entrega
del parche.

Si una persona no estuvo directamente involucrada en la preparación o
administración de un parche pero desea expresar y registrar su aprobación,
entonces puede pedir que se agregue una línea Acked-by: al registro de
cambios del parche.

Acked-by: a menudo lo usa el maintainer del código afectado cuando ese
maintainer no contribuyó ni envió el parche.

Acked-by: no es tan formal como Signed-off-by:. Es una manera de marcar que
el "acker" ha revisado al menos ese parche y ha indicado su aceptación. Por
los merge de parches a veces convertirán manualmente el "sí, me parece bien"
de un acker en un Acked-by: (pero tenga en cuenta que por lo general es
mejor pedir un acuse de recibo explícito).

Acked-by: no necesariamente indica el reconocimiento de todo el parche.
Por ejemplo, si un parche afecta a varios subsistemas y tiene un
Acked-by: de un maintainer del subsistema, entonces esto generalmente
indica el reconocimiento de solo la parte que afecta el código de ese
maintainer. Buen juicio debe ejercitarse aquí. En caso de duda, la gente
debe consultar la discusión original en los archivos de la lista de correo.

Si una persona ha tenido la oportunidad de comentar un parche, pero no lo
ha hecho, puede incluir opcionalmente una etiqueta ``Cc:`` al parche.
Esta es la única etiqueta que se puede agregar sin una acción explícita por
parte de la persona a la que se nombre - pero debe indicar que esta persona
fue copiada en el parche. Esta etiqueta documenta que las partes
potencialmente interesadas han sido incluidas en la discusión.

Co-developed-by: establece que el parche fue co-creado por múltiples
desarrolladores; se utiliza para dar atribución a los coautores (además del
autor atribuido por la etiqueta From:) cuando varias personas trabajan en
un solo parche. Ya que Co-developed-by: denota autoría, cada
Co-developed-by: debe ser inmediatamente seguido de Signed-off-by: del
coautor asociado. Se mantiene el procedimiento estándar, es decir, el orden
de las etiquetas Signed-off-by: debe reflejar el historial cronológico del
parche en la medida de lo posible, independientemente de si el autor se
atribuye a través de From: o Co-developed-by:. Cabe destacar que el último
Signed-off-by: siempre debe ser del desarrollador que envía el parche.

Tenga en cuenta que la etiqueta From: es opcional cuando el autor From: es
también la persona (y correo electrónico) enumerados en la línea From: del
encabezado del correo electrónico.

Ejemplo de un parche enviado por el From: autor::

	<changelog>

	Co-developed-by: Primer coautor <primer@coauthor.example.org>
	Signed-off-by: Primer coautor <primer@coauthor.example.org>
	Co-developed-by: Segundo coautor <segundo@coautor.ejemplo.org>
	Signed-off-by: Segundo coautor <segundo@coautor.ejemplo.org>
	Signed-off-by: Autor del From <from@author.example.org>

Ejemplo de un parche enviado por un Co-developed-by: autor::

	From: Autor del From <from@author.example.org>

	<changelog>

	Co-developed-by: Co-Autor aleatorio <aleatorio@coauthor.example.org>
	Signed-off-by: Coautor aleatorio <aleatorio@coauthor.example.org>
	Signed-off-by: Autor del From <from@author.example.org>
	Co-developed-by: Coautor que envió <sub@coauthor.example.org>
	Signed-off-by: Coautor que envía <sub@coauthor.example.org>

Uso de Reported-by:, Tested-by:, Reviewed-by:, Suggested-by: y Fixes:
----------------------------------------------------------------------

La etiqueta Reported-by (Reportado-por) otorga crédito a las personas que
encuentran errores y los reportan. Por favor, tenga en cuenta que si se
informó de un error en privado, debe pedir primero permiso antes de usar la
etiqueta Reported-by. La etiqueta está destinada a errores; por favor no la
use para acreditar peticiones de características.

Una etiqueta Tested-by: indica que el parche se probó con éxito (en algún
entorno) por la persona nombrada. Esta etiqueta informa a los maintainers
de que se han realizado algunas pruebas, proporciona un medio para ubicar
"testers" (gente que pruebe) otros parches futuros y asegura el crédito
para los testers.

Reviewed-by: en cambio, indica que el parche ha sido revisado y encontrado
aceptable de acuerdo con la Declaración del Revisor:

Declaración de Supervisión del Revisor
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Al ofrecer mi etiqueta Reviewed-by:, afirmo que:

(a) He llevado a cabo una revisión técnica de este parche para
evaluar su idoneidad y preparación para su inclusión en
el kernel principal.

(b) Cualquier problema, inquietud o pregunta relacionada con el parche
han sido comunicados al remitente. Estoy satisfecho
con la respuesta del remitente a mis comentarios.

(c) Si bien puede haber cosas que podrían mejorarse con esta
entrega, creo que es, en este momento, (1) una
modificación valiosa al kernel, y (2) libre de conocidas
cuestiones que argumentarían en contra de su inclusión.

(d) Si bien he revisado el parche y creo que es correcto,
no hago (a menos que se indique explícitamente en otro lugar) ninguna
garantía o avales de que logrará su definido
propósito o función en cualquier situación dada.

Una etiqueta Reviewed-by es una declaración de opinión de que el parche es
una modificación apropiada al kernel sin que haya ningún problema grave
a nivel técnico. Cualquier revisor interesado (que haya hecho el trabajo)
puede ofrecer una etiqueta Reviewed-by para un parche. Esta etiqueta sirve
para dar crédito a revisores e informar a los maintainers del grado de
revisión que se ha hecho en el parche. Las etiquetas Reviewed-by, cuando
las otorgan revisores conocidos por entender del tema y realizar
revisiones exhaustivas, normalmente aumentan la probabilidad de que su
parche entre en el kernel.

Las etiquetas Tested-by y Reviewed-by, una vez recibidas en la lista de
correo por el tester o revisor, deben ser incluidas por el autor de los
parches pertinentes al enviar próximas versiones. Sin embargo, si el parche
ha cambiado sustancialmente en la siguiente versión, es posible que estas
etiquetas ya no sean aplicables y, por lo tanto, deben eliminarse. Por lo
general, se debe mencionar la eliminación de las etiquetas Tested-by o
Reviewed-by de alguien en el registro de cambios del parche (después del
separador '---').

Una etiqueta Suggested-by: indica que la idea del parche es sugerida por la
persona nombrada y asegura el crédito a la persona por la idea. Tenga en
cuenta que esto no debe agregarse sin el permiso del "reporter",
especialmente si la idea no fue publicada en un foro público. Dicho esto,
si diligentemente acreditamos a los reporters de ideas, con suerte, se
sentirán inspirados para ayudarnos nuevamente en el futuro.

Una etiqueta Fixes: indica que el parche corrige un problema en un commit
anterior. Esto se utiliza para facilitar descubrir dónde se originó un
error, lo que puede ayudar a revisar una corrección de errores. Esta
etiqueta también ayuda al equipo del kernel estable a determinar qué
versiones estables del kernel deberían recibir su corrección. Este es el
método preferido para indicar un error corregido por el parche. Revise
:ref:`describe_changes` para más detalles.

Nota: Adjuntar una etiqueta Fixes: no subvierte las reglas estables del
proceso del kernel ni el requisito de CC: stable@vger.kernel.org en todos
los parches candidatos de ramas estables. Para obtener más información, lea
Documentation/process/stable-kernel-rules.rst.

.. _sp_the_canonical_patch_format:

Formato de parche canónico
---------------------------

Esta sección describe cómo debe darse formato al propio parche. Tenga en
cuenta que, si tiene sus parches almacenados en un repositorio ``git``, el
parche con formato adecuado se puede obtener con ``git format-patch``. Las
herramientas no pueden crear el texto necesario, sin embargo, así que lea
las instrucciones a continuación de todos modos.

La línea de asunto del parche canónico es::

    Asunto: [PATCH 001/123] subsistema: frase de resumen

El cuerpo del mensaje del parche canónico contiene lo siguiente:

  - Una línea ``from`` que especifica el autor del parche, seguida de una
    línea vacía (solo es necesario si la persona que envía el parche no es
    el autor).

  - El cuerpo de la explicación, línea envuelta en 75 columnas, que se
    copiara en el registro de cambios permanente para describir este parche.

  - Una línea vacía.

  - Las líneas ``Signed-off-by:``, descritas anteriormente, que
    también vaya en el registro de cambios.

  - Una línea de marcador que contiene simplemente ``---``.

  - Cualquier comentario adicional que no sea adecuado para el registro de
    cambios.

  - El parche real (output de ``diff``).

El formato de la línea de asunto hace que sea muy fácil ordenar los correos
electrónicos alfabéticamente por línea de asunto - prácticamente cualquier
lector de correo electrónico permite esto, ya que debido a que el número de
secuencia se rellena con ceros, el orden numérico y alfabético es el mismo.

El ``subsistema`` en el asunto del correo electrónico debe identificar qué
área o subsistema del kernel está siendo parcheado.

La ``frase de resumen`` en el Asunto del correo electrónico debe describir
de forma concisa el parche que contiene ese correo electrónico. La
``frase resumen`` no debe ser un nombre de archivo. No use la mismo ``frase
resumen`` para cada parche en una serie completa de parches (donde una
`` serie de parches`` (patch series) es una secuencia ordenada de múltiples
parches relacionados).

Tenga en cuenta que la ``frase de resumen`` de su correo electrónico se
convierte en un identificador global único para ese parche. Se propaga por
hasta el registro de cambios de ``git``. La ``frase resumida`` se puede
usar más adelante en discusiones de desarrolladores que se refieran al
parche. La gente querrá buscar en Google la ``frase de resumen`` para leer
la discusión al respecto del parche. También será lo único que la gente
podrá ver rápidamente cuando, dos o tres meses después, estén pasando por
quizás miles de parches usando herramientas como ``gitk`` o ``git log
--oneline``.

Por estas razones, el ``resumen`` no debe tener más de 70-75 caracteres, y
debe describir tanto lo que cambia el parche como por qué el parche podría
ser necesario. Es un reto ser tanto sucinto como descriptivo, pero eso es
lo que un resumen bien escrito debería hacer.

La ``frase de resumen`` puede estar precedida por etiquetas encerradas en
corchetes: "Asunto: [PATCH <etiqueta>...] <frase de resumen>". Las
etiquetas no se consideran parte de la frase de resumen, pero describen
cómo debería ser tratado el parche. Las etiquetas comunes pueden incluir un
descriptor de versión si las múltiples versiones del parche se han enviado
en respuesta a comentarios (es decir, "v1, v2, v3") o "RFC" para indicar
una solicitud de comentarios.

Si hay cuatro parches en una serie de parches, los parches individuales
pueden enumerarse así: 1/4, 2/4, 3/4, 4/4. Esto asegura que los
desarrolladores entiendan el orden en que se deben aplicar los parches y
que han revisado o aplicado todos los parches de la serie de parches.

Aquí hay algunos buenos ejemplos de Asuntos::

    Asunto: [PATCH 2/5] ext2: mejorar la escalabilidad de la búsqueda de mapas de bits
    Asunto: [PATCH v2 27/01] x86: corregir el seguimiento de eflags
    Asunto: [PATCH v2] sub/sys: resumen conciso del parche
    Asunto: [PATCH v2 M/N] sub/sys: resumen conciso del parche

La línea ``from`` debe ser la primera línea en el cuerpo del mensaje,
y tiene la forma::

        From: Autor del parche <autor@ejemplo.com>

La línea ``From`` especifica quién será acreditado como el autor del parche
en el registro de cambios permanente. Si falta la línea ``from``, entonces
la línea ``From:`` del encabezado del correo electrónico se usará para
determinar el autor del parche en el registro de cambios.

La explicación estará incluida en el commit del changelog permanente, por
lo que debería tener sentido para un lector competente que hace mucho tiempo
ha olvidado los detalles de la discusión que podrían haber llevado a
este parche. Incluidos los síntomas del fallo que el parche trate
(mensajes de registro del kernel, mensajes de oops, etc.) son especialmente
útiles para personas que podrían estar buscando en los registros de
commits en busca de la aplicación del parche. El texto debe estar escrito
con tal detalle que cuando se lea semanas, meses o incluso años después,
pueda dar al lector la información necesaria y detalles para comprender el
razonamiento de **por qué** se creó el parche.

Si un parche corrige una falla de compilación, puede que no sea necesario
incluir _todos_ los errores de compilación; pero lo suficiente como para
que sea probable que alguien que busque el parche puede encontrarlo. Como
en la ``frase de resumen``, es importante ser tanto sucinto como
descriptivo.

La línea marcadora ``---`` cumple el propósito esencial de marcar para
herramientas de manejo de parches donde termina el mensaje de registro de
cambios.

Un buen uso de los comentarios adicionales después del marcador ``---`` es
para ``diffstat``, para mostrar qué archivos han cambiado, y el número de
líneas insertadas y eliminadas por archivo. Un ``diffstat`` es
especialmente útil en parches más grandes. Si va a incluir un ``diffstat``
después del marcador ``---``, utilice las opciones ``diffstat``
``-p 1 -w 70`` para que los nombres de archivo se enumeran desde la parte
superior del árbol de fuentes del kernel y no use demasiado espacio
horizontal (que encaje fácilmente en 80 columnas, tal vez con alguna
indentación). (``git`` genera diffstats apropiados por defecto).

Otros comentarios relevantes solo en el momento o para el maintainer, pero
no adecuados para el registro de cambios permanente, también debe ir aquí.
Un buen ejemplo de tales comentarios podría ser ``registros de cambios de
parches`` que describen qué ha cambiado entre la versión v1 y v2 del
parche.

Por favor, ponga esta información **después** de la línea ``---`` que
separa el registro de cambios del resto del parche. La información de la
versión no forma parte del registro de cambios que se incluye con el árbol
git. Es información adicional para los revisores. Si se coloca encima de la
etiquetas de commit, necesita interacción manual para eliminarlo. Si esta
debajo de la línea de separación, se quita automáticamente al aplicar el
parche::

  <mensaje de commit>
  ...
  Signed-off-by: Autor <autor@correo>
  ---
  V2 -> V3: función auxiliar redundante eliminada
  V1 -> V2: estilo de código limpio y comentarios de revisión abordados

  ruta/al/archivo | 5+++--
  ...

Revise más detalles sobre el formato de parche adecuado en las siguientes
referencias

.. _sp_backtraces:

Retrocesos en mensajes de confirmación
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Los "backtraces" (deshacer el camino) ayuda a documentar la cadena de
llamadas que conducen a un problema. Sin embargo, no todos los rastreos son
útiles. Por ejemplo, las tempranas cadenas de llamadas de inicio son únicas
y obvias. Sin embargo, al copiar la salida completa de dmesg textualmente,
incluye información que distrae, como marcas de tiempo, listas de módulos,
registro y volcados de pila.

Por lo tanto, los backtraces más útiles deben contener los datos
relevantes de la información vertida, lo que hace que sea más fácil
centrarse en el verdadero tema. Este es un ejemplo de un backtrace bien
recortado::

  error de acceso de MSR no verificado: WRMSR a 0xd51 (intentó escribir 0x0000000000000064)
  en rIP: 0xffffffffae059994 (native_write_msr+0x4/0x20)
  Rastreo de llamadas:
  mba_wrmsr
  update_domains
  rdtgroup_mkdir

.. _sp_explicit_in_reply_to:

In-Reply-To explicitos en las cabeceras
---------------------------------------

Puede ser útil agregar manualmente encabezados In-Reply-To: a un parche
(por ejemplo, al usar ``git send-email``) para asociar el parche con una
discusión anterior relevante, por ejemplo para vincular una corrección de
errores al correo electrónico con el informe de errores. Sin embargo, para
una serie de parches múltiples, generalmente es mejor evitar usar
In-Reply-To: para vincular a versiones anteriores de la serie. De esta
forma, varias versiones del parche no se convierten en un inmanejable
bosque de referencias en clientes de correo electrónico. Si un enlace es
útil, puede usar el redirector https://lore.kernel.org/ (por ejemplo, en
el texto de la carta de introducción del correo electrónico) para vincular
a una versión anterior de la serie de parches.


Proporcionar información de árbol base
--------------------------------------

Cuando otros desarrolladores reciben sus parches y comienzan el proceso de
revisión, a menudo es útil para ellos saber en qué parte del historial del
árbol deben colocar su trabajo. Esto es particularmente útil para CI
automatizado de procesos que intentan ejecutar una serie de pruebas para
establecer la calidad de su envío antes de que el maintainer comience la
revisión.

Si está utilizando ``git format-patch`` para generar sus parches, puede
incluir automáticamente la información del árbol base en su envío usando el
parámetro ``--base``. La forma más fácil y conveniente de usar esta opción
es con "topical branches" (ramas de temas)::

    $ git checkout -t -b my-topical-branch master
    Branch 'my-topical-branch' set up to track local branch 'master'.
    Switched to a new branch 'my-topical-branch'

    [realice sus cambios y ediciones]

    $ git format-patch --base=auto --cover-letter -o outgoing/ master
    outgoing/0000-cover-letter.patch
    outgoing/0001-First-Commit.patch
    outgoing/...

Cuando abra ``outgoing/0000-cover-letter.patch`` para editar, tenga en
cuenta que tendrá el tráiler ``base-commit:`` al final, que proporciona al
revisor y a las herramientas de CI suficiente información para realizar
correctamente ``git am`` sin preocuparse por los conflictos::

    $ git checkout -b patch-review [base-commit-id]
    Switched to a new branch 'patch-review'
    $ git am patches.mbox
    Applying: First Commit
    Applying: ...

Consulte ``man git-format-patch`` para obtener más información al respecto
de esta opción.

.. Note::

    La función ``--base`` se introdujo en la versión 2.9.0 de git.

Si no está utilizando git para dar forma a sus parches, aún puede incluir
el mismo tráiler ``base-commit`` para indicar el hash de confirmación del
árbol en que se basa su trabajo. Debe agregarlo en la carta de presentación
o en el primer parche de la serie y debe colocarse ya sea bajo la línea
``---`` o en la parte inferior de todos los demás contenido, justo antes de
su firma del correo electrónico.


Referencias
-----------

"The perfect patch" (tpp) por Andrew Morton.
  <https://www.ozlabs.org/~akpm/stuff/tpp.txt>

"Linux kernel patch submission format" por Jeff Garzik.
  <https://web.archive.org/web/20180829112450/http://linux.yyz.us/patch-format.html>

"How to piss off a kernel subsystem maintainer" por Greg Kroah-Hartman.
  <http://www.kroah.com/log/linux/maintainer.html>

  <http://www.kroah.com/log/linux/maintainer-02.html>

  <http://www.kroah.com/log/linux/maintainer-03.html>

  <http://www.kroah.com/log/linux/maintainer-04.html>

  <http://www.kroah.com/log/linux/maintainer-05.html>

  <http://www.kroah.com/log/linux/maintainer-06.html>

Kernel Documentation/process/coding-style.rst

Email de Linus Torvalds sobre la forma canónica de los parches:
  <https://lore.kernel.org/r/Pine.LNX.4.58.0504071023190.28951@ppc970.osdl.org>

"On submitting kernel patches" por Andi Kleen
  Algunas estrategias para conseguir incluir cambios complicados o
  controvertidos.

  http://halobates.de/on-submitting-patches.pdf
