.. SPDX-License-Identifier: GPL-2.0

:Original: :ref:`Documentation/process/researcher-guidelines.rst`
:Translator: Avadhut Naik <avadhut.naik@amd.com>

Directrices para Investigadores
++++++++++++++++++++++++++++++++

La comunidad del kernel de Linux da la bienvenida a la investigación
transparente sobre el kernel de Linux, las actividades involucradas
en su producción, otros subproductos de su desarrollo. Linux se
beneficia mucho de este tipo de investigación, y la mayoría de los
aspectos de Linux son impulsados por investigación en una forma u otra.

La comunidad agradece mucho si los investigadores pueden compartir
los hallazgos preliminares antes de hacer públicos sus resultados,
especialmente si tal investigación involucra seguridad. Involucrarse
temprano ayuda a mejorar la calidad de investigación y la capacidad
de Linux para mejorar a partir de ella. En cualquier caso, se recomienda
compartir copias de acceso abierto de la investigación publicada con
la comunidad.

Este documento busca clarificar lo que la comunidad del kernel de Linux
considera practicas aceptables y no aceptables al llevar a cabo
investigación de este tipo. Por lo menos, dicha investigación y
actividades afines deben seguir las reglas estándar de ética de la
investigación. Para más información sobre la ética de la investigación
en general, ética en la tecnología y la investigación de las comunidades
de desarrolladores en particular, ver:


* `Historia de la Ética en la Investigación <https://www.unlv.edu/research/ORI-HSR/history-ethics>`_
* `Ética de la IEEE <https://www.ieee.org/about/ethics/index.html>`_
* `Perspectivas de Desarrolladores e Investigadores sobre la Ética de los Experimentos en Proyectos de Código Abierto <https://arxiv.org/pdf/2112.13217.pdf>`_

La comunidad del kernel de Linux espera que todos los que interactúan con
el proyecto están participando en buena fe para mejorar Linux. La
investigación sobre cualquier artefacto disponible públicamente (incluido,
pero no limitado a código fuente) producido por la comunidad del kernel
de Linux es bienvenida, aunque la investigación sobre los desarrolladores
debe ser claramente opcional.

La investigación pasiva que se basa completamente en fuentes disponibles
públicamente, incluidas las publicaciones en listas de correo públicas y
las contribuciones a los repositorios públicos, es claramente permitida.
Aunque, como con cualquier investigación, todavía se debe seguir la ética
estándar.

La investigación activa sobre el comportamiento de los desarrolladores,
sin embargo, debe hacerse con el acuerdo explícito y la divulgación
completa a los desarrolladores individuales involucrados. No se puede
interactuar / experimentar con los desarrolladores sin consentimiento;
esto también es ética de investigación estándar.

Para ayudar a aclarar: enviar parches a los desarrolladores es interactuar
con ellos, pero ya han dado su consentimiento para recibir contribuciones
en buena fe. No se ha dado consentimiento para enviar parches intencionalmente
defectuosos / vulnerables o contribuir con la información engañosa a las
discusiones. Dicha comunicación puede ser perjudicial al desarrollador (por
ejemplo, agotar el tiempo, el esfuerzo, y la moral) y perjudicial para el
proyecto al erosionar la confianza de toda la comunidad de desarrolladores en
el colaborador (y la organización del colaborador en conjunto), socavando
los esfuerzos para proporcionar reacciones constructivas a los colaboradores
y poniendo a los usuarios finales en riesgo de fallas de software.

La participación en el desarrollo de Linux en sí mismo por parte de
investigadores, como con cualquiera, es bienvenida y alentada. La
investigación del código de Linux es una práctica común, especialmente
cuando se trata de desarrollar o ejecutar herramientas de análisis que
producen resultados procesables.

Cuando se interactúa con la comunidad de desarrolladores, enviar un
parche ha sido tradicionalmente la mejor manera para hacer un impacto.
Linux ya tiene muchos errores conocidos – lo que es mucho más útil es
tener soluciones verificadas. Antes de contribuir, lea cuidadosamente
la documentación adecuada.

* Documentation/process/development-process.rst
* Documentation/process/submitting-patches.rst
* Documentation/admin-guide/reporting-issues.rst
* Documentation/process/security-bugs.rst

Entonces envíe un parche (incluyendo un registro de confirmación con
todos los detalles enumerados abajo) y haga un seguimiento de cualquier
comentario de otros desarrolladores.

* ¿Cuál es el problema específico que se ha encontrado?
* ¿Como podría llegar al problema en un sistema en ejecución?
* ¿Qué efecto tendría encontrar el problema en el sistema?
* ¿Como se encontró el problema? Incluya específicamente detalles sobre
  cualquier prueba, programas de análisis estáticos o dinámicos, y cualquier
  otra herramienta o método utilizado para realizar el trabajo.
* ¿En qué versión de Linux se encontró el problema? Se prefiere usar la
  versión más reciente o una rama reciente de linux-next (ver
  Documentation/process/howto.rst).
* ¿Que se cambió para solucionar el problema y por qué se cree es correcto?
* ¿Como se probó el cambio para la complicación y el tiempo de ejecución?
* ¿Qué confirmación previa corrige este cambio? Esto debería ir en un “Fixes:”
  etiqueta como se describe en la documentación.
* ¿Quién más ha revisado este parche? Esto debería ir con la adecuada “Reviewed-by”
  etiqueta; Vea abajo.

Por ejemplo (en inglés, pues es en las listas)::

  From: Author <author@email>
  Subject: [PATCH] drivers/foo_bar: Add missing kfree()

  The error path in foo_bar driver does not correctly free the allocated
  struct foo_bar_info. This can happen if the attached foo_bar device
  rejects the initialization packets sent during foo_bar_probe(). This
  would result in a 64 byte slab memory leak once per device attach,
  wasting memory resources over time.

  This flaw was found using an experimental static analysis tool we are
  developing, LeakMagic[1], which reported the following warning when
  analyzing the v5.15 kernel release:

   path/to/foo_bar.c:187: missing kfree() call?

  Add the missing kfree() to the error path. No other references to
  this memory exist outside the probe function, so this is the only
  place it can be freed.

  x86_64 and arm64 defconfig builds with CONFIG_FOO_BAR=y using GCC
  11.2 show no new warnings, and LeakMagic no longer warns about this
  code path. As we don't have a FooBar device to test with, no runtime
  testing was able to be performed.

  [1] https://url/to/leakmagic/details

  Reported-by: Researcher <researcher@email>
  Fixes: aaaabbbbccccdddd ("Introduce support for FooBar")
  Signed-off-by: Author <author@email>
  Reviewed-by: Reviewer <reviewer@email>

Si usted es un colaborador por primera vez, se recomienda que el parche en
si sea examinado por otros en privado antes de ser publicado en listas
públicas. (Esto es necesario si se le ha dicho explícitamente que sus parches
necesitan una revisión interna más cuidadosa.) Se espera que estas personas
tengan su etiqueta “Reviewed-by” incluida en el parche resultante. Encontrar
otro desarrollador con conocimiento de las contribuciones a Linux, especialmente
dentro de su propia organización, y tener su ayuda con las revisiones antes de
enviarlas a las listas de correo publico tiende a mejorar significativamente la
calidad de los parches resultantes, y reduce así la carga de otros desarrolladores.

Si no se puede encontrar a nadie para revisar internamente los parches y necesita
ayuda para encontrar a esa persona, o si tiene alguna otra pregunta relacionada
con este documento y las expectativas de la comunidad de desarrolladores, por
favor contacte con la lista de correo privada Technical Advisory Board:
<tech-board@lists.linux-foundation.org>.
